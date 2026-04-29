"""
Spotify OAuth helper — run this once to get a refresh token for the ESP32.

Usage:
  1. Create a Spotify app at developer.spotify.com/dashboard
  2. Set redirect URI to http://127.0.0.1:8888/callback
  3. Run: python get_spotify_token.py
  4. Paste the refresh token into src/config.h
"""

import http.server
import urllib.parse
import urllib.request
import json
import webbrowser
import base64

CLIENT_ID = input("Enter your Spotify Client ID: ").strip()
CLIENT_SECRET = input("Enter your Spotify Client Secret: ").strip()

REDIRECT_URI = "http://127.0.0.1:8888/callback"
SCOPES = "user-read-playback-state user-modify-playback-state user-read-currently-playing"

auth_code = None

class CallbackHandler(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        global auth_code
        query = urllib.parse.urlparse(self.path).query
        params = urllib.parse.parse_qs(query)

        if "code" in params:
            auth_code = params["code"][0]
            self.send_response(200)
            self.send_header("Content-Type", "text/html")
            self.end_headers()
            self.wfile.write(b"<h1>Success! You can close this tab.</h1>")
        else:
            self.send_response(400)
            self.send_header("Content-Type", "text/html")
            self.end_headers()
            error = params.get("error", ["unknown"])[0]
            self.wfile.write(f"<h1>Error: {error}</h1>".encode())

    def log_message(self, format, *args):
        pass  # Suppress request logs

# Open browser for authorization
auth_url = (
    "https://accounts.spotify.com/authorize?"
    + urllib.parse.urlencode({
        "client_id": CLIENT_ID,
        "response_type": "code",
        "redirect_uri": REDIRECT_URI,
        "scope": SCOPES,
    })
)

print(f"\nOpening browser for Spotify login...")
print(f"If it doesn't open, go to:\n{auth_url}\n")
webbrowser.open(auth_url)

# Wait for callback
server = http.server.HTTPServer(("localhost", 8888), CallbackHandler)
print("Waiting for authorization...")
while auth_code is None:
    server.handle_request()
server.server_close()

# Exchange code for tokens
print("Exchanging code for tokens...")
auth_header = base64.b64encode(f"{CLIENT_ID}:{CLIENT_SECRET}".encode()).decode()
data = urllib.parse.urlencode({
    "grant_type": "authorization_code",
    "code": auth_code,
    "redirect_uri": REDIRECT_URI,
}).encode()

req = urllib.request.Request(
    "https://accounts.spotify.com/api/token",
    data=data,
    headers={
        "Authorization": f"Basic {auth_header}",
        "Content-Type": "application/x-www-form-urlencoded",
    },
)

with urllib.request.urlopen(req) as resp:
    tokens = json.loads(resp.read())

refresh_token = tokens["refresh_token"]

print(f"\n{'='*60}")
print(f"REFRESH TOKEN (paste into src/config.h):")
print(f"{'='*60}")
print(refresh_token)
print(f"{'='*60}")
print(f"\nThis token does not expire unless you revoke it.")
