# sdk/python/tests/conftest.py
"""Pytest configuration and global fixtures for USMP Python SDK."""

import pytest

from usmp._handshake import _failed_handshakes


@pytest.fixture(autouse=True)
def reset_rate_limiter_state():
    """Isolate test runs by resetting the global rate-limiter state before and after each test."""
    _failed_handshakes.clear()
    yield
    _failed_handshakes.clear()
