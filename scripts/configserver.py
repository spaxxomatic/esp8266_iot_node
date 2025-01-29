# python -m http.server 8000 --directory ./my_dir

from http.server import HTTPServer as BaseHTTPServer, SimpleHTTPRequestHandler
import os
from urllib.parse import urlparse, unquote

def get_conn_info(mac):
    return "trestie_2G 11213141 192.168.1.5 mqttactor mqttpass"
    
class HTTPHandler(SimpleHTTPRequestHandler):
    """This handler uses server.base_path instead of always using os.getcwd()"""

    def translate_path(self, path):
        path = SimpleHTTPRequestHandler.translate_path(self, path)
        relpath = os.path.relpath(path, os.getcwd())
        fullpath = os.path.join(self.server.base_path, relpath)
        return fullpath
    def do_PUT(self):
        path = self.translate_path(self.path)
        length = int(self.headers['Content-Length'])        
        print(self.rfile.read(length))
        self.send_response(200, "OK")        

    def do_GET(self):
        parsed_url = urlparse(self.path)
        if parsed_url.path.startswith("/connstr/"):
            # Extract the identifier from the path
            identifier = unquote(parsed_url.path[len("/connstr/"):])
            print (identifier)        
            # Respond with a connection string or similar info
            conn_info = get_conn_info(identifier)
            self.send_response(200)
            self.send_header("Content-type", "text/html")
            self.end_headers()
            self.wfile.write(conn_info.encode('utf-8'))
        else:
            # Default behavior for other paths
            super().do_GET()

class HTTPServer(BaseHTTPServer):
    """The main server, you pass in base_path which is the path you want to serve requests from"""

    def __init__(self, base_path, server_address, RequestHandlerClass=HTTPHandler):
        self.base_path = base_path
        BaseHTTPServer.__init__(self, server_address, RequestHandlerClass)

def start_config_server():
    web_dir = os.path.join(os.path.dirname(__file__), 'my_dir')
    httpd = HTTPServer(web_dir, ("", 80))
    httpd.serve_forever()