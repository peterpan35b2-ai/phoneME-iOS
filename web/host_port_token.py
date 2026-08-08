import hmac
import ipaddress
import os
import re
import socket

from websockify.token_plugins import BasePlugin


class HostPortToken(BasePlugin):
    """Resolve a Websockify token of the form host:port to a TCP target.

    This plugin is intended for the local phoneME development proxy only. The
    container is bound to loopback so it cannot become an unauthenticated
    internet-facing TCP relay by accident.
    """

    _HOST = re.compile(r"^[A-Za-z0-9._-]+$")

    def __init__(self, source=None):
        super().__init__(source)

    def lookup(self, token):
        if not token:
            return None

        required_access = os.environ.get("PHONEME_PROXY_ACCESS_TOKEN", "")
        target = token
        if required_access:
            supplied_access, separator, target = token.partition("@")
            if not separator or not hmac.compare_digest(supplied_access, required_access):
                return None

        if ":" not in target:
            return None
        host, raw_port = target.rsplit(":", 1)
        if not self._HOST.fullmatch(host):
            return None
        try:
            port = int(raw_port)
        except ValueError:
            return None
        if port < 1 or port > 65535:
            return None
        public_mode = os.environ.get("PHONEME_PROXY_PUBLIC_MODE", "") == "1"
        if public_mode:
            try:
                addresses = {
                    ipaddress.ip_address(item[4][0])
                    for item in socket.getaddrinfo(host, port, type=socket.SOCK_STREAM)
                }
            except (OSError, ValueError):
                return None
            if not addresses or any(
                address.is_private
                or address.is_loopback
                or address.is_link_local
                or address.is_multicast
                or address.is_reserved
                or address.is_unspecified
                for address in addresses
            ):
                return None
        elif host in {"127.0.0.1", "localhost"}:
            host = "host.docker.internal"
        return [host, str(port)]
