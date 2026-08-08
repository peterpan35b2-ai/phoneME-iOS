from urllib.parse import parse_qs, urlparse

from websockify import websocketproxy


_original_get_target = websocketproxy.ProxyRequestHandler.get_target


def _get_target_compat(self, target_plugin):
    if self.host_token:
        return _original_get_target(self, target_plugin)

    args = parse_qs(urlparse(self.path).query)
    tokens = args.get("token") or []
    if not tokens:
        return _original_get_target(self, target_plugin)

    token = tokens[0].rstrip("\n")
    access = (args.get("access") or [""])[0]
    if access and "@" not in token:
        token = f"{access}@{token}"

    result_pair = target_plugin.lookup(token)
    if result_pair is not None:
        return result_pair
    raise self.server.EClose("Token not found")


websocketproxy.ProxyRequestHandler.get_target = _get_target_compat

if __name__ == "__main__":
    websocketproxy.websockify_init()
