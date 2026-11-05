#!/usr/bin/env python3
"""fake_igd.py --mode MODE --ports FILE -- a UPnP Internet Gateway Device on loopback only,
for desktop/tests/online.  An SSDP responder (UDP 127.0.0.1:random) and the device's HTTP
server (TCP 127.0.0.1:random); writes "SSDP_PORT HTTP_PORT" to FILE once both listen.
Runs until killed, or until a GET /quit.

MODE:
  ok          WAN 203.0.113.7 (TEST-NET-3, stands for a public address); mappings with leases
  permanent   AddPortMapping with a lease -> 725 OnlyPermanentLeasesSupported; lease 0 works
  conflict    UDP 27420 (and 37420) already forwarded to 192.168.1.99 -> 718
  ours        UDP 37420 already forwarded to 127.0.0.1:37420 (an earlier run of ours)
  cgnat       WAN 100.72.1.2 (carrier-grade NAT)
  private     WAN 192.168.0.5 (a second router in front: double NAT)
  refuse      AddPortMapping -> 501 ActionFailed
  nossdp      never answers M-SEARCH
  noservice   a device description with no WAN connection service
  chunked     the description is sent with chunked transfer coding; WANPPPConnection
  evil        SSDP points at http://8.8.8.8/desc.xml (must never be followed)

GET /state -> {"mappings": {...}, "adds": N, "deletes": N, "location_hits": N}
"""
import argparse
import json
import os
import re
import socket
import socketserver
import threading
from http.server import BaseHTTPRequestHandler, HTTPServer

ap = argparse.ArgumentParser()
ap.add_argument("--mode", default="ok")
ap.add_argument("--ports", required=True)
A = ap.parse_args()
MODE = A.mode
WAN = {"cgnat": "100.72.1.2", "private": "192.168.0.5"}.get(MODE, "203.0.113.7")
SERVICE = "urn:schemas-upnp-org:service:WANPPPConnection:1" if MODE == "chunked" else \
    "urn:schemas-upnp-org:service:WANIPConnection:1"
state = {"mappings": {}, "adds": 0, "deletes": 0, "location_hits": 0}
lock = threading.Lock()
if MODE == "conflict":
    for p in (27420, 37420):
        state["mappings"]["%d/UDP" % p] = {"client": "192.168.1.99", "port": p, "lease": 0, "desc": "someone else"}
if MODE == "ours":
    state["mappings"]["37420/UDP"] = {"client": "127.0.0.1", "port": 37420, "lease": 3600, "desc": "GBAdhoc XMB relay"}


def desc_xml():
    if MODE == "noservice":
        svc = "<service><serviceType>urn:schemas-upnp-org:service:Layer3Forwarding:1</serviceType><controlURL>/l3f</controlURL></service>"
    else:
        svc = ("<service><serviceType>urn:schemas-upnp-org:service:WANCommonInterfaceConfig:1</serviceType>"
               "<controlURL>/wcic</controlURL></service>"
               "<service><serviceType>%s</serviceType><serviceId>urn:upnp-org:serviceId:WANConn1</serviceId>"
               "<controlURL>/ctl/IPConn</controlURL><SCPDURL>/scpd.xml</SCPDURL></service>" % SERVICE)
    return ('<?xml version="1.0"?><root xmlns="urn:schemas-upnp-org:device-1-0"><specVersion><major>1</major>'
            '<minor>0</minor></specVersion><device><deviceType>urn:schemas-upnp-org:device:InternetGatewayDevice:1'
            '</deviceType><friendlyName>Fake Router (%s)</friendlyName><deviceList><device><deviceType>'
            'urn:schemas-upnp-org:device:WANDevice:1</deviceType><deviceList><device><deviceType>'
            'urn:schemas-upnp-org:device:WANConnectionDevice:1</deviceType><serviceList>%s</serviceList>'
            '</device></deviceList></device></deviceList></device></root>' % (MODE, svc))


def arg(body, name):
    m = re.search(r"<%s>(.*?)</%s>" % (name, name), body, re.S)
    return m.group(1).strip() if m else ""


def fault(code, text):
    return 500, ('<?xml version="1.0"?><s:Envelope xmlns:s="http://schemas.xmlsoap.org/soap/envelope/"><s:Body>'
                 '<s:Fault><faultcode>s:Client</faultcode><faultstring>UPnPError</faultstring><detail>'
                 '<UPnPError xmlns="urn:schemas-upnp-org:control-1-0"><errorCode>%d</errorCode>'
                 '<errorDescription>%s</errorDescription></UPnPError></detail></s:Fault></s:Body></s:Envelope>'
                 % (code, text))


def ok(action, inner=""):
    return 200, ('<?xml version="1.0"?><s:Envelope xmlns:s="http://schemas.xmlsoap.org/soap/envelope/"><s:Body>'
                 '<u:%sResponse xmlns:u="%s">%s</u:%sResponse></s:Body></s:Envelope>' % (action, SERVICE, inner, action))


def soap(action, body):
    with lock:
        if action == "GetExternalIPAddress":
            return ok(action, "<NewExternalIPAddress>%s</NewExternalIPAddress>" % WAN)
        key = "%s/%s" % (arg(body, "NewExternalPort"), arg(body, "NewProtocol"))
        if action == "AddPortMapping":
            lease = int(arg(body, "NewLeaseDuration") or "0")
            client = arg(body, "NewInternalClient")
            if MODE == "refuse":
                return fault(501, "ActionFailed")
            if MODE == "permanent" and lease != 0:
                return fault(725, "OnlyPermanentLeasesSupported")
            cur = state["mappings"].get(key)
            if cur and cur["client"] != client:
                return fault(718, "ConflictInMappingEntry")
            if cur and MODE == "ours":
                return fault(718, "ConflictInMappingEntry")   # some routers say 718 even for the same client
            state["mappings"][key] = {"client": client, "port": int(arg(body, "NewInternalPort") or 0),
                                      "lease": lease, "desc": arg(body, "NewPortMappingDescription")}
            state["adds"] += 1
            return ok(action)
        if action == "DeletePortMapping":
            if key not in state["mappings"]:
                return fault(714, "NoSuchEntryInArray")
            del state["mappings"][key]
            state["deletes"] += 1
            return ok(action)
        if action == "GetSpecificPortMappingEntry":
            cur = state["mappings"].get(key)
            if not cur:
                return fault(714, "NoSuchEntryInArray")
            return ok(action, "<NewInternalPort>%d</NewInternalPort><NewInternalClient>%s</NewInternalClient>"
                              "<NewEnabled>1</NewEnabled><NewPortMappingDescription>%s</NewPortMappingDescription>"
                              "<NewLeaseDuration>%d</NewLeaseDuration>" % (cur["port"], cur["client"], cur["desc"], cur["lease"]))
    return fault(401, "Invalid Action")


class H(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def log_message(self, *a):
        pass

    def send(self, status, body, ctype="text/xml", chunked=False):
        data = body.encode()
        self.send_response(status)
        self.send_header("Content-Type", ctype)
        self.send_header("Connection", "close")
        if chunked:
            self.send_header("Transfer-Encoding", "chunked")
            self.end_headers()
            for i in range(0, len(data), 100):
                c = data[i:i + 100]
                self.wfile.write(b"%x\r\n%s\r\n" % (len(c), c))
            self.wfile.write(b"0\r\n\r\n")
        else:
            self.send_header("Content-Length", str(len(data)))
            self.end_headers()
            self.wfile.write(data)
        self.close_connection = True

    def do_GET(self):
        if self.path == "/desc.xml":
            with lock:
                state["location_hits"] += 1
            self.send(200, desc_xml(), chunked=(MODE == "chunked"))
        elif self.path == "/state":
            with lock:
                self.send(200, json.dumps(state), "application/json")
        elif self.path == "/quit":
            self.send(200, "bye", "text/plain")
            threading.Timer(0.2, lambda: os._exit(0)).start()
        else:
            self.send(404, "no", "text/plain")

    def do_POST(self):
        n = int(self.headers.get("Content-Length") or 0)
        body = self.rfile.read(n).decode("utf-8", "replace")
        action = (self.headers.get("SOAPAction") or "").strip('"').split("#")[-1]
        st, xml = soap(action, body)
        self.send(st, xml)


class TS(socketserver.ThreadingMixIn, HTTPServer):
    daemon_threads = True


httpd = TS(("127.0.0.1", 0), H)
hport = httpd.server_address[1]
ssdp = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
ssdp.bind(("127.0.0.1", 0))
sport = ssdp.getsockname()[1]


def ssdp_loop():
    while True:
        data, addr = ssdp.recvfrom(2048)
        if MODE == "nossdp" or not data.startswith(b"M-SEARCH"):
            continue
        st = re.search(rb"ST:\s*(\S+)", data)
        loc = "http://8.8.8.8/desc.xml" if MODE == "evil" else "http://127.0.0.1:%d/desc.xml" % hport
        ssdp.sendto(("HTTP/1.1 200 OK\r\nCACHE-CONTROL: max-age=120\r\nEXT:\r\nLOCATION: %s\r\nSERVER: Fake/1.0 UPnP/1.0\r\n"
                     "ST: %s\r\nUSN: uuid:fake::%s\r\n\r\n" % (loc, st.group(1).decode() if st else "upnp:rootdevice",
                                                              st.group(1).decode() if st else "")).encode(), addr)


threading.Thread(target=ssdp_loop, daemon=True).start()
with open(A.ports, "w", newline="\n") as f:
    f.write("%d %d\n" % (sport, hport))
httpd.serve_forever()
