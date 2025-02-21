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

def find_firmware_bin(start_path):
    for root, dirs, files in os.walk(start_path):
        if "firmware.bin" in files:
            return os.path.join(root, "firmware.bin")
    return None  # Return None if not found

#
# Push new firmware to the upload server
#

def publish_firmware(source, target, env):
    firmware_path = find_firmware_bin(os.path.join(".pio", "build"))
    
    env_name = env["PIOENV"]
    print(f"Environment: {env_name}")
    print(f"Firmware path: {firmware_path}")
    
    firmware_name = basename(firmware_path)
    
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
    
    target_name = str(version) + ".bin"
    
    if (upload_method == 'HTTP_POST_UPLOAD'):
        r = None
        try:
            r = requests.post(url,
                            files={"file":(target_name, open(firmware_path, "rb"))}
                            )
            r.raise_for_status()
        except requests.exceptions.RequestException as e:
            sys.stderr.write("Failed to submit package: %s\n" %
                            ("%s\n%s" % (r.status_code, r.text) if r else str(e)))
            env.Exit(1)

    print("Firmware " + target_name + " has been successfuly uploaded to ota repo")


# Custom upload command and program name
# Define a custom target called "runscript"
env.AddCustomTarget(
    name="publish_firmware",
    dependencies=None,  # No dependencies, so it doesn’t trigger a build
    actions=[
        "pio --version",
        publish_firmware
    ],
    title="Upload fw to ota server",
    description="Executes a custom script without compilation"
)

env.Replace(PROGNAME="firmware_v_%s" % version, UPLOADCMD=publish_firmware)