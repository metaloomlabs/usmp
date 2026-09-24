import io
import logging

from usmp._logging import ColoredFormatter, setup_logging


def test_setup_logging():
    buf = io.StringIO()
    handler = setup_logging(level=logging.INFO, stream=buf, color=True)

    logger = logging.getLogger("usmp.test")
    logger.info("Testing colored log output")

    output = buf.getvalue()
    assert handler is not None
    assert "Testing colored log output" in output
    assert "INFO" in output
    assert "\033[" in output  # Verify ANSI color escape sequence present
    assert "test" in output
    assert "[\033[97;1mtest\033[90m]" in output  # Verify styled namespace


def test_setup_logging_no_color():
    buf = io.StringIO()
    handler = setup_logging(level=logging.INFO, stream=buf, color=False)

    logger = logging.getLogger("usmp.server")
    logger.info("Server listening on port 9000")

    output = buf.getvalue()
    assert handler is not None
    assert "\033[" not in output  # No ANSI escapes
    assert "[INFO ]" in output
    assert "[server]" in output
    assert "Server listening on port 9000" in output


def test_colored_formatter_levels_and_namespace():
    formatter_color = ColoredFormatter(use_color=True)
    formatter_plain = ColoredFormatter(use_color=False)

    # Test namespace shortening
    rec_server = logging.LogRecord("usmp.server", logging.INFO, "test.py", 1, "hello", (), None)
    assert "[server]" in formatter_plain.format(rec_server)

    rec_core = logging.LogRecord("usmp", logging.WARN, "test.py", 1, "warning msg", (), None)
    assert "[core]" in formatter_plain.format(rec_core)

    rec_custom = logging.LogRecord("custom_app", logging.DEBUG, "test.py", 1, "debug msg", (), None)
    assert "[custom_app]" in formatter_plain.format(rec_custom)

    # Test error and critical pill badges
    rec_err = logging.LogRecord(
        "usmp.transport", logging.ERROR, "test.py", 1, "error msg", (), None
    )
    colored_err = formatter_color.format(rec_err)
    assert "ERROR" in colored_err
    assert "\033[41;97;1m" in colored_err

    # Test exception formatting
    try:
        raise ValueError("simulated test error")
    except ValueError:
        import sys

        rec_exc = logging.LogRecord(
            "usmp.client", logging.ERROR, "test.py", 1, "caught error", (), sys.exc_info()
        )
        exc_output = formatter_color.format(rec_exc)
        assert "ValueError: simulated test error" in exc_output
