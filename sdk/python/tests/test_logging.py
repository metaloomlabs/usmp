import io
import logging
import usmp
from usmp._logging import ColoredFormatter, setup_logging


def test_setup_logging():
    buf = io.StringIO()
    handler = setup_logging(level=logging.INFO, stream=buf, color=True)

    logger = logging.getLogger("usmp.test")
    logger.info("Testing colored log output")

    output = buf.getvalue()
    assert "Testing colored log output" in output
    assert "INFO" in output
    assert "\033[" in output  # Verify ANSI color escape sequence present
