#!/usr/bin/env python3
"""Provision two physically unlocked boards over their USB consoles. No RF pairing."""
import argparse
import json
import os
from pathlib import Path
import re
import secrets
import select
import termios
import time

class Console:
    def __init__(self, port):
        self.fd=os.open(port,os.O_RDWR|os.O_NOCTTY|os.O_NONBLOCK)
        try:
            settings=termios.tcgetattr(self.fd)
            settings[0]=settings[1]=settings[3]=0
            settings[2]=termios.CS8|termios.CREAD|termios.CLOCAL
            settings[4]=settings[5]=termios.B115200
            settings[6][termios.VMIN]=settings[6][termios.VTIME]=0
            termios.tcsetattr(self.fd,termios.TCSANOW,settings)
            termios.tcflush(self.fd,termios.TCIOFLUSH)
        except BaseException:
            os.close(self.fd)
            raise
        self.pending=b""

    def command(self,text):
        data=(text+"\n").encode("ascii");deadline=time.monotonic()+5
        while data:
            if not select.select([], [self.fd], [], max(0,deadline-time.monotonic()))[1]:
                raise RuntimeError("USB write timed out.")
            try:
                data=data[os.write(self.fd,data):]
            except BlockingIOError:
                pass
        while time.monotonic()<deadline:
            while b"\n" in self.pending:
                line,self.pending=self.pending.split(b"\n",1)
                try:
                    value=json.loads(line)
                except (ValueError,UnicodeDecodeError):
                    continue
                if isinstance(value,dict) and "ok" in value:
                    if not value["ok"]:
                        raise RuntimeError(value.get("error","Board rejected request."))
                    return value
            if select.select([self.fd],[],[],max(0,deadline-time.monotonic()))[0]:
                try:
                    self.pending+=os.read(self.fd,512)
                except BlockingIOError:
                    pass
                if len(self.pending)>4096:
                    raise RuntimeError("Malformed USB response.")
        raise RuntimeError("No board response; check the port and close other serial monitors.")

    def close(self):
        os.close(self.fd)

def validate_info(info,role):
    if info.get("role")!=role or not re.fullmatch(r"[0-9a-fA-F]{12}",info.get("mac","")):
        raise ValueError("The USB port does not identify the expected firmware role.")
    return info["mac"].lower()

def credentials():
    return {"psk":secrets.token_hex(32),"lmk":secrets.token_hex(16),"pmk":secrets.token_hex(16)}

def pair_command(peer,keys,channel):
    if not re.fullmatch(r"[0-9a-f]{12}",peer) or not 1<=channel<=11:
        raise ValueError("Invalid peer/channel.")
    for name,n in (("psk",64),("lmk",32),("pmk",32)):
        if not re.fullmatch(r"[0-9a-f]{"+str(n)+"}",keys[name]):
            raise ValueError("Invalid key material.")
    return "PAIR "+peer+" "+keys["psk"]+" "+keys["lmk"]+" "+keys["pmk"]+" "+str(channel)

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--helmet",required=True)
    parser.add_argument("--paw",required=True)
    parser.add_argument("--channel",type=int,choices=range(1,12),default=6)
    args=parser.parse_args()
    if os.path.realpath(args.helmet)==os.path.realpath(args.paw):
        parser.error("Use two different USB ports.")
    opened=[]
    try:
        helmet=Console(args.helmet);opened.append(helmet)
        paw=Console(args.paw);opened.append(paw)
        helmet_mac=validate_info(helmet.command("INFO"),1)
        paw_mac=validate_info(paw.command("INFO"),2)
        if helmet_mac==paw_mac:
            raise ValueError("Both ports identify the same radio.")
        # User physically unlocks each before invoking this command. A failed
        # half-pair is safe; rerunning installs a fresh common pair on both.
        keys=credentials()
        helmet.command(pair_command(paw_mac,keys,args.channel))
        try:
            paw.command(pair_command(helmet_mac,keys,args.channel))
        except Exception as error:
            raise RuntimeError("Helmet was provisioned but paw was not. Unlock both and rerun; partial pairing accepts no paw commands.") from error
        print("Both boards provisioned with unique keys. They restart and connect on channel",args.channel)
        print("No key files were saved. Test disconnect/reconnect and button behavior on the bench.")
    finally:
        for console in opened:
            console.close()

if __name__=="__main__":
    try:
        main()
    except (RuntimeError,ValueError,OSError) as error:
        raise SystemExit(str(error))
