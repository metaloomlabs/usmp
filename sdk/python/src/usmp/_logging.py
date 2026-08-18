import logging
import sys
from typing import Any


class ColoredFormatter(logging.Formatter):
    """
    Custom ANSI Color Formatter for USMP logs with clean status symbols and timestamp/level styling.
    """

    COLORS = {
        logging.DEBUG: "\033[36m",      # Cyan
        logging.INFO: "\033[32m",       # Green
        logging.WARNING: "\033[33m",    # Yellow
        logging.ERROR: "\033[31m",      # Red
        logging.CRITICAL: "\033[35m",   # Magenta
    }
    RESET = "\033[0m"
    BOLD = "\033[1m"
    DIM = "\033[2m"

    def __init__(self, fmt: str | None = None, datefmt: str | None = None, use_color: bool = True):
        super().__init__(fmt=fmt, datefmt=datefmt)
        self.use_color = use_color

    def format(self, record: logging.LogRecord) -> str:
        if not self.use_color:
            return super().format(record)

        color = self.COLORS.get(record.levelno, self.RESET)
        level_name = record.levelname
        timestamp = self.formatTime(record, "%H:%M:%S")
        name = record.name
        msg = record.getMessage()

        # Format: 12:00:00 [INFO   ] usmp.server: Message
        formatted = (
            f"{self.DIM}{timestamp}{self.RESET} "
            f"{color}{self.BOLD}[{level_name:<7}]{self.RESET} "
            f"{self.DIM}{name}:{self.RESET} {msg}"
        )
        if record.exc_info:
            formatted += "\n" + self.formatException(record.exc_info)
        return formatted


def setup_logging(
    level: int | str = logging.INFO,
    stream: Any = sys.stderr,
    fmt: str | None = None,
    color: bool | None = None,
) -> logging.Handler:
    """
    Configures rich, colored logging for the USMP library (`usmp` logger).

    Usage:
        import usmp
        usmp.setup_logging()
    """
    if isinstance(level, str):
        level = getattr(logging, level.upper(), logging.INFO)

    logger = logging.getLogger("usmp")
    logger.setLevel(level)

    # Remove existing non-Null Handlers to avoid duplicate log entries
    for handler in logger.handlers[:]:
        if not isinstance(handler, logging.NullHandler):
            logger.removeHandler(handler)

    handler = logging.StreamHandler(stream)
    handler.setLevel(level)

    if color is None:
        color = hasattr(stream, "isatty") and stream.isatty()

    handler.setFormatter(ColoredFormatter(fmt=fmt, use_color=color))
    logger.addHandler(handler)
    return handler
