import requests
import sys
from os.path import basename
import os 

Import('env')

try:
    import configparser
except ImportError:
    import ConfigParser as configparser
project_config = configparser.ConfigParser()
project_config.read("platformio.ini")
VERSION_FILE = "version"
#version = project_config.get("common", "release_version")
version = 0
try:
        with open(VERSION_FILE) as FILE:
            version = FILE.readline()            
            version = int(version)

except:
        print('No version file found or cannot read')

#
# Push new firmware to the upload server
#

def publish_firmware(source, target, env):
    firmware_path = str(source[0])
    firmware_name = basename(firmware_path)

    print("Uploading {0} to fw repository. Version: {1}".format(
        firmware_name, version))

    url = "/".join([
        "http://192.168.1.5:9999", "upload"
    ])

    headers = {
        #"Content-type": "application/octet-stream"
    }
    
    upload_method = 'HTTP_POST_UPLOAD'
    
    if (upload_method == 'SAMBA_UPLOAD'):    
        server_share = '\\\\192.168.1.11\\nutiu\\fw_server\\'
        cmd = " ".join(('cp', firmware_path, server_share + str(version) + '.bin'))
        print (cmd)
        os.system(cmd)

    if (upload_method == 'HTTP_POST_UPLOAD'):
        r = None
        try:
            r = requests.post(url,
                            files={"files":(str(version) + ".bin", open(firmware_path, "rb"))}
                            )
            r.raise_for_status()
        except requests.exceptions.RequestException as e:
            sys.stderr.write("Failed to submit package: %s\n" %
                            ("%s\n%s" % (r.status_code, r.text) if r else str(e)))
            env.Exit(1)

    print("The firmware has been successfuly uploaded to ota repo")


# Custom upload command and program name
env.Replace(PROGNAME="firmware_v_%s" % version, UPLOADCMD=publish_firmware)