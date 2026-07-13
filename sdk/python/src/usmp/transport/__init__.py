from typing import Type

from .base import USMPListener, USMPTransport
from .tcp import TCPListener, TCPTransport
from .udp import UDPListener, UDPStream

_transports: dict[str, Type[USMPTransport]] = {}
_listeners: dict[str, Type[USMPListener]] = {}


def register_transport(
    name: str,
    transport_cls: Type[USMPTransport],
    listener_cls: Type[USMPListener],
) -> None:
    """Register a custom transport and its corresponding server listener."""
    _transports[name.lower()] = transport_cls
    _listeners[name.lower()] = listener_cls


def get_transport_class(name: str) -> Type[USMPTransport]:
    """Retrieve the transport class registered for the given protocol name."""
    name_lower = name.lower()
    if name_lower not in _transports:
        raise ValueError(f"Unknown transport protocol: '{name}'")
    return _transports[name_lower]


def get_listener_class(name: str) -> Type[USMPListener]:
    """Retrieve the listener class registered for the given protocol name."""
    name_lower = name.lower()
    if name_lower not in _listeners:
        raise ValueError(f"Unknown transport protocol: '{name}'")
    return _listeners[name_lower]


# Register core protocols
register_transport("tcp", TCPTransport, TCPListener)
register_transport("udp", UDPStream, UDPListener)
