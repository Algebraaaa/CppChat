'use strict';

// This loopback gateway protects the existing C++ HTTP service without changing
// its protocol. Remote clients reach it through Nginx TLS or an SSH tunnel.
const http = require('node:http');
const fs = require('node:fs');
const crypto = require('node:crypto');
const net = require('node:net');
const Redis = require(process.env.CPPCHAT_REDIS_MODULE || 'ioredis');
const config = JSON.parse(fs.readFileSync(process.env.CPPCHAT_VERIFY_CONFIG, 'utf8'));
const port = Number(process.env.CPPCHAT_GUARD_PORT || 8090);
const upstreamPort = Number(process.env.CPPCHAT_GATE_PORT || 18080);
const proxySecret = process.env.CPPCHAT_PROXY_SECRET_FILE ?
  fs.readFileSync(process.env.CPPCHAT_PROXY_SECRET_FILE, 'utf8').trim() : '';
const proxySecretBytes = Buffer.from(proxySecret);
if (proxySecret && proxySecret.length < 32) throw Error('Invalid proxy secret');
function clientAddress(request) {
  const remote = request.socket.remoteAddress || '';
  const supplied = request.headers['x-cppchat-proxy'];
  const address = request.headers['x-real-ip'];
  if (proxySecret && (remote === '127.0.0.1' || remote === '::1' || remote === '::ffff:127.0.0.1') &&
      typeof supplied === 'string' && Buffer.byteLength(supplied) === proxySecretBytes.length &&
      crypto.timingSafeEqual(Buffer.from(supplied), proxySecretBytes) &&
      typeof address === 'string' && net.isIP(address)) return address;
  return remote;
}
for (const value of [port, upstreamPort]) {
  if (!Number.isInteger(value) || value < 1 || value > 65535) throw Error('Invalid port');
}
const redis = new Redis({ ...config.redis, connectTimeout: 2000,
  commandTimeout: 2000, maxRetriesPerRequest: 1, enableOfflineQueue: false });
redis.on('error', () => console.error('GatewayGuard Redis unavailable'));
const routes = new Set(['/get_verifycode', '/user_login', '/user_register', '/reset_pwd']);
const hash = value => crypto.createHash('sha256').update(value).digest('hex');
const keyPrefix = process.env.CPPCHAT_GUARD_KEY_PREFIX || 'security:';
const counterScript = `
  for i,key in ipairs(KEYS) do
    local limit = tonumber(ARGV[(i-1)*2+1])
    local count = tonumber(redis.call('GET',key) or '0')
    if count >= limit then return math.max(redis.call('TTL',key),1) end
  end
  for i,key in ipairs(KEYS) do
    local count = redis.call('INCR',key)
    if count == 1 then redis.call('EXPIRE',key,tonumber(ARGV[(i-1)*2+2])) end
  end
  return 0`;
const failureScript = `
  local count = redis.call('INCR',KEYS[1])
  if count == 1 then redis.call('EXPIRE',KEYS[1],ARGV[1]) end
  return count`;

function reply(response, status, error, message, retryAfter) {
  if (response.destroyed || response.writableEnded) return;
  const headers = { 'Content-Type': 'application/json; charset=utf-8',
    'Cache-Control': 'no-store', 'X-Content-Type-Options': 'nosniff' };
  if (retryAfter) headers['Retry-After'] = String(retryAfter);
  response.writeHead(status, headers);
  response.end(JSON.stringify({ error, message, ...(retryAfter ? { retry_after: retryAfter } : {}) }));
}

function readBody(request) {
  return new Promise((resolve, reject) => {
    const chunks = []; let size = 0;
    request.on('data', chunk => {
      size += chunk.length;
      if (size > 32768) { reject(Object.assign(Error('Body limit'), { status: 413 })); return; }
      chunks.push(chunk);
    });
    request.on('end', () => resolve(Buffer.concat(chunks)));
    request.on('aborted', () => reject(Error('Client disconnected')));
    request.on('error', reject);
  });
}

function forward(route, body) {
  return new Promise((resolve, reject) => {
    const upstream = http.request({ host: '127.0.0.1', port: upstreamPort,
      path: route, method: 'POST', headers: { 'Content-Type': 'application/json',
        'Content-Length': body.length } }, response => {
      const chunks = []; let size = 0;
      response.on('data', chunk => {
        size += chunk.length;
        if (size > 131072) { response.destroy(Error('Upstream body limit')); return; }
        chunks.push(chunk);
      });
      response.on('end', () => resolve({ status: response.statusCode, body: Buffer.concat(chunks) }));
      response.on('error', reject);
    });
    upstream.setTimeout(25000, () => upstream.destroy(Error('Upstream timeout')));
    upstream.on('error', reject);
    upstream.end(body);
  });
}

const server = http.createServer(async (request, response) => {
  try {
    if (request.method !== 'POST' || !routes.has(request.url)) {
      reply(response, 404, 1001, 'Unknown endpoint'); return;
    }
    const body = await readBody(request);
    let data;
    try { data = JSON.parse(body.toString('utf8')); }
    catch { reply(response, 400, 1001, 'Invalid JSON'); return; }
    if (!data || typeof data.email !== 'string' || data.email.length > 255 ||
        !/^[^\s@]+@[^\s@]+\.[^\s@]+$/.test(data.email)) {
      reply(response, 400, 1001, 'Invalid email'); return;
    }
    const account = hash(data.email.trim().toLowerCase());
    const failureKey = keyPrefix + 'failure:' + (request.url === '/user_login' ? 'login:' : 'verify:') + account;
    if (request.url !== '/get_verifycode' && Number(await redis.get(failureKey) || 0) >= 5) {
      const wait = Math.max(await redis.ttl(failureKey), 1);
      reply(response, 429, 1016, 'Too many failed attempts', wait); return;
    }
    const rules = [[keyPrefix + 'requests:' + hash(clientAddress(request)), 120, 60]];
    if (request.url === '/get_verifycode') {
      rules.push([keyPrefix + 'mail:cooldown:' + account, 1, 60],
        [keyPrefix + 'mail:hour:' + account, 5, 3600], [keyPrefix + 'mail:global', 20, 3600]);
    } else if (request.url === '/user_login') {
      rules.push([keyPrefix + 'login:attempts:' + account, 10, 300]);
    } else {
      // Registration and password reset share the same six-digit code budget.
      rules.push([keyPrefix + 'verify:attempts:' + account, 5, 180]);
    }
    const wait = Number(await redis.eval(counterScript, rules.length,
      ...rules.map(rule => rule[0]), ...rules.flatMap(rule => rule.slice(1))));
    if (wait) { reply(response, 429, 1016, 'Too many requests', wait); return; }
    const result = await forward(request.url, body);
    const value = JSON.parse(result.body.toString('utf8'));
    if ((request.url === '/user_login' && value.error === 1009) ||
        (request.url !== '/get_verifycode' && [1003, 1004].includes(value.error))) {
      await redis.eval(failureScript, 1, failureKey, request.url === '/user_login' ? 300 : 180);
    } else if (request.url !== '/get_verifycode' && value.error === 0) {
      await redis.del(failureKey);
    }
    if (!response.destroyed) {
      response.writeHead(result.status, { 'Content-Type': 'application/json; charset=utf-8',
        'Cache-Control': 'no-store', 'X-Content-Type-Options': 'nosniff' });
      response.end(result.body);
    }
  } catch (error) {
    // Never log bodies, emails, passwords, verification codes or login tokens.
    console.error('GatewayGuard request failed: ' + (error.status || 'dependency failure'));
    reply(response, error.status || 503, error.status ? 1001 : 1016, 'Request unavailable');
  }
});
server.maxConnections = 128;
server.maxHeadersCount = 32;
server.headersTimeout = 15000;
server.requestTimeout = 20000;
server.keepAliveTimeout = 5000;
server.on('clientError', (_, socket) => socket.destroy());
server.on('error', error => { console.error('GatewayGuard listener failed: ' + error.code); process.exitCode = 1; });
redis.once('ready', () => server.listen(port, '127.0.0.1', () => console.log('GatewayGuard ready on loopback')));
function stop() { server.close(() => redis.quit().finally(() => process.exit(0))); }
process.on('SIGTERM', stop);
process.on('SIGINT', stop);
