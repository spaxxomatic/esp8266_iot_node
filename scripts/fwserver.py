import os
from http.server import BaseHTTPRequestHandler, HTTPServer
import socketserver
import cgi

# Server settings
HOST = "0.0.0.0"
PORT = 9999
UPLOAD_DIR = "espfw"

# Ensure the upload directory exists
os.makedirs(UPLOAD_DIR, exist_ok=True)

class EspFwHandler(BaseHTTPRequestHandler):
    def do_GET(self):
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.end_headers()
        

    def do_POST(self):
        """Handle file upload at /upload"""
        if self.path != "/upload":
            self.send_response(404)
            self.end_headers()
            self.wfile.write(b"Page Not Found")
            return

        # Parse multipart form data
        content_type = self.headers.get("Content-Type")
        if not content_type or "multipart/form-data" not in content_type:
            self.send_response(400)
            self.end_headers()
            self.wfile.write(b"Invalid Content-Type")
            return

        form = cgi.FieldStorage(fp=self.rfile, headers=self.headers, environ={"REQUEST_METHOD": "POST"})
        file_item = form["file"]

        if file_item.filename:
            filepath = os.path.join(UPLOAD_DIR, file_item.filename)
            with open(filepath, "wb") as f:
                f.write(file_item.file.read())

            self.send_response(200)
            self.end_headers()
            self.wfile.write(f"File {file_item.filename} uploaded successfully".encode())
        else:
            self.send_response(400)
            self.end_headers()
            self.wfile.write(b"No file uploaded")
    
    def do_GET(self):
        """Serve FW files from the fw directory"""
        print("\n=== GET Request Headers ===")
        for header, value in self.headers.items():
            print(f"{header}: {value}")

        file_path = self.path.lstrip("/")  # Remove leading slash
        localfwfilepath = os.path.join(UPLOAD_DIR, file_path)
        if not os.path.isfile(localfwfilepath):
            self.send_response(404)
            self.end_headers()
            self.wfile.write(b"File %b not found"%(localfwfilepath.encode()))
            return

        self.send_response(200)
        self.send_header("Content-Type", "application/octet-stream")
        self.end_headers()

        with open(localfwfilepath, "rb") as file:
            self.wfile.write(file.read())

# Run the server
#with socketserver.TCPServer((HOST, PORT), FileUploadHandler) as httpd:    
#    httpd.serve_forever()
if __name__ == "__main__":
    server = HTTPServer(("0.0.0.0", 9999), EspFwHandler)
    print(f"Server running at http://{HOST}:{PORT}/upload")
    server.serve_forever()