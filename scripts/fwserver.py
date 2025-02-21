import os
from http.server import BaseHTTPRequestHandler, HTTPServer
import cgi

# Server settings
HOST = "0.0.0.0"
PORT = 9999
UPLOAD_DIR = "espfw"

# Ensure the upload directory exists
os.makedirs(UPLOAD_DIR, exist_ok=True)

class EspFwHandler(BaseHTTPRequestHandler):
    def do_POST(self):
        """Handle file upload at /upload"""
        if self.path != "/upload":
            self.send_response(404)
            self.end_headers()
            self.wfile.write(b"Upload only to /upload endpoint")
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
            safe_filename = os.path.basename(file_item.filename)
            filepath = os.path.join(UPLOAD_DIR, safe_filename)
            
            try:
                with open(filepath, "wb") as f:
                    f.write(file_item.file.read())

                self.send_response(200)
                self.end_headers()
                self.wfile.write(f"File {safe_filename} uploaded successfully".encode())

            except Exception as e:
                self.send_response(500)
                self.end_headers()
                self.wfile.write(f"File upload failed: {str(e)}".encode())
        else:
            self.send_response(400)
            self.end_headers()
            self.wfile.write(b"No file uploaded")
    
    def do_GET(self):
        """Serve FW files from the fw directory"""
        print("\n=== GET Request Headers ===")
        for header, value in self.headers.items():
            print(f"{header}: {value}")

        if self.path == "/list":
            try:
                files = os.listdir(UPLOAD_DIR)
                self.send_response(200)
                self.send_header("Content-Type", "application/json")
                self.end_headers()
                self.wfile.write(str(files).encode())
            except Exception as e:
                self.send_response(500)
                self.end_headers()
                self.wfile.write(f"Error listing files: {str(e)}".encode())
            return
        
        file_path = self.path.lstrip("/")  # Remove leading slash
        localfwfilepath = os.path.join(UPLOAD_DIR, file_path)
        if not os.path.isfile(localfwfilepath):
            self.send_response(404)
            self.end_headers()
            self.wfile.write(f"File {file_path} not found".encode())
            return
        
        file_size = os.path.getsize(localfwfilepath)  # Get file size

        self.send_response(200)
        self.send_header("Content-Type", "application/octet-stream")
        self.send_header("Content-Length", str(file_size))  # Set Content-Length
        self.end_headers()
        
        with open(localfwfilepath, "rb") as file:
            self.wfile.write(file.read())

if __name__ == "__main__":
    server = HTTPServer(("0.0.0.0", 9999), EspFwHandler)
    print(f"Server running at http://{HOST}:{PORT}/upload")
    server.serve_forever()