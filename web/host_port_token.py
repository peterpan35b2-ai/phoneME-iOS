import re

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
        if not token or ":" not in token:
            return None
        host, raw_port = token.rsplit(":", 1)
        if not self._HOST.fullmatch(host):
            return None
        try:
            port = int(raw_port)
        except ValueError:
            return None
        if port < 1 or port > 65535:
            return None
        if host in {"127.0.0.1", "localhost"}:
            host = "host.docker.internal"
        return [host, str(port)]
