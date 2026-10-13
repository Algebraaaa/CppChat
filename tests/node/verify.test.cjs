const { test } = require('node:test');
const assert = require('node:assert/strict');
// Import must succeed even with no config.json or installed transport packages.
const { createHandlers, VERIFY_CODE_TTL_SECONDS } = require('../../VerifyServer/server');
const call = (handler, email) => new Promise(resolve =>
  handler({ request: { email } }, (error, response) => { assert.equal(error, null); resolve(response); }));

test('AUTH-OTP: six digits, EX 180, mail and repeated request', async () => {
  const values = new Map(), messages = [], saved = [];
  const handler = createHandlers({ config: { emailUser: 'sender@example.test' },
    email: { sendEmail: async message => messages.push(message) },
    redis: { getValue: async key => values.get(key) ?? null,
      setValueWithExpiration: async (key, value, ttl) => {
        saved.push(ttl); values.set(key, value); return true;
      } } }).GetVerifyCode;
  assert.equal((await call(handler, 'one@example.test')).error, 0);
  assert.match(values.get('code_one@example.test'), /^[1-9][0-9]{5}$/);
  assert.equal((await call(handler, 'one@example.test')).error, 0);
  assert.deepEqual(saved, [VERIFY_CODE_TTL_SECONDS]);
  assert.equal(saved[0], 180);
  assert.equal(messages.length, 2);
  assert.equal(messages[0].text, messages[1].text);
  assert.equal(messages[0].to, 'one@example.test');
});
test('AUTH-DEPENDENCY: failed Redis save never mails or reports success', async () => {
  let mailed = false;
  const handler = createHandlers({ config: {}, email: { sendEmail: async () => { mailed = true; } },
    redis: { getValue: async () => null, setValueWithExpiration: async () => false } }).GetVerifyCode;
  assert.notEqual((await call(handler, 'one@example.test')).error, 0);
  assert.equal(mailed, false);
});
test('AUTH-DEPENDENCY: SMTP failure is an error', async () => {
  const handler = createHandlers({ config: {}, email: { sendEmail: async () => { throw Error('SMTP failed'); } },
    redis: { getValue: async () => '123456' } }).GetVerifyCode;
  assert.notEqual((await call(handler, 'one@example.test')).error, 0);
});
