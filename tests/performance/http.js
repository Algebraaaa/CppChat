import http from 'k6/http';
import { check } from 'k6';

// k6 handles HTTP only. Custom chat/resource framing is exercised by asyncio.
export const options = {
  scenarios: { gateway: { executor: 'constant-arrival-rate', rate: 100,
    timeUnit: '1s', duration: __ENV.DURATION || '10m', preAllocatedVUs: 20, maxVUs: 100 } },
  thresholds: { checks: ['rate==1'], http_req_failed: ['rate==0'] },
};
export default function () {
  const response = http.get(`${__ENV.GATE_URL}/get_test`);
  check(response, { 'HTTP 200': reply => reply.status === 200 });
}
