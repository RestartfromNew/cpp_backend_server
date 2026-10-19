"""Opt-in TEST-account HTTP acceptance checks; requires a running migrated server."""
import concurrent.futures
import json
import os
import unittest
import urllib.error
import urllib.request


class LoginTicketHTTPTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        required = ["LOGIN_TICKET_TEST_URL", "LOGIN_TICKET_EMAIL",
                    "LOGIN_TICKET_PASSWORD", "LOGIN_TICKET_DEVICE_ID"]
        if not all(os.environ.get(key) for key in required):
            raise unittest.SkipTest("Set LOGIN_TICKET_* variables for a TEST server/account/device")
        cls.base = os.environ[required[0]].rstrip("/")
        cls.email = os.environ[required[1]]
        cls.password = os.environ[required[2]]
        cls.device = os.environ[required[3]]

    def post(self, path, body):
        request = urllib.request.Request(
            self.base + path, data=json.dumps(body).encode(), method="POST",
            headers={"Content-Type": "application/json"})
        try:
            with urllib.request.urlopen(request, timeout=15) as response:
                return response.status, json.load(response)
        except urllib.error.HTTPError as error:
            return error.code, json.load(error)

    def ticket(self):
        status, body = self.post("/login_by_email", {"email": self.email, "password": self.password})
        self.assertEqual(status, 200)
        self.assertEqual(body.get("code"), "login_success")
        self.assertEqual(len(body["login_ticket"]), 64)
        self.assertNotIn("access_token", body)
        self.assertNotIn("refresh_token", body)
        return body["login_ticket"]

    def complete(self, ticket, device=None):
        return self.post("/device_verification", {
            "login_ticket": ticket, "device_id": device or self.device,
            "user_id": "00000000-0000-0000-0000-000000000000"})

    def test_no_ticket_cannot_mint_tokens(self):
        status, body = self.post("/device_verification", {"device_id": self.device})
        self.assertEqual(status, 401)
        self.assertEqual(body["error"]["code"], "invalid_login_ticket")
        status, _ = self.post("/register_device", {})
        self.assertEqual(status, 401)

    def test_password_failure_has_no_ticket(self):
        status, body = self.post("/login_by_email", {
            "email": self.email, "password": self.password + "-wrong"})
        self.assertEqual(status, 401)
        self.assertEqual(body, {"code": "invalid_credentials"})

    def test_completion_and_replay(self):
        ticket = self.ticket()
        status, body = self.complete(ticket)
        self.assertEqual(status, 200)
        self.assertEqual(body["device_id"], self.device)
        self.assertNotEqual(body["id"], "00000000-0000-0000-0000-000000000000")
        self.assertTrue(body["access_token"] and body["refresh_token"])
        status, body = self.complete(ticket)
        self.assertEqual(status, 401)
        self.assertEqual(body["error"]["code"], "invalid_login_ticket")

    def test_concurrent_single_use(self):
        ticket = self.ticket()
        with concurrent.futures.ThreadPoolExecutor(max_workers=2) as executor:
            statuses = sorted(executor.map(lambda _: self.complete(ticket)[0], range(2)))
        self.assertEqual(statuses, [200, 401])

    def test_bad_device_does_not_consume(self):
        ticket = self.ticket()
        status, _ = self.complete(ticket, "bad-uuid")
        self.assertEqual(status, 400)
        status, _ = self.complete(ticket)
        self.assertEqual(status, 200)

    def test_foreign_device(self):
        foreign = os.environ.get("LOGIN_TICKET_FOREIGN_DEVICE_ID")
        if not foreign:
            self.skipTest("Set LOGIN_TICKET_FOREIGN_DEVICE_ID for the ownership test")
        ticket = self.ticket()
        status, body = self.complete(ticket, foreign)
        self.assertEqual(status, 403)
        self.assertEqual(body["error"]["code"], "device_not_owned")
        self.assertEqual(self.complete(ticket)[0], 200)


if __name__ == "__main__":
    unittest.main()
