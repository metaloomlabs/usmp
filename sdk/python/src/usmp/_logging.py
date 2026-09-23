import logging
import sys
from typing import Any, ClassVar


class ColoredFormatter(logging.Formatter):
    """
    Sleek Dev-Tool ANSI Color Formatter for USMP logs with pill badges,
    dimmed timestamps, and shortened logger namespaces.
    """

    LEVEL_BADGES: ClassVar[dict[int, str]] = {
        logging.DEBUG: "\033[46;30m DEBUG \033[0m",       # Cyan pill
        logging.INFO: "\033[42;30m INFO  \033[0m",       # Emerald green pill
        logging.WARNING: "\033[43;30m WARN  \033[0m",    # Amber/yellow pill
        logging.ERROR: "\033[41;97;1m ERROR \033[0m",    # Bold white on red pill
        logging.CRITICAL: "\033[45;97;1m CRIT  \033[0m", # Bold white on magenta pill
    }

    RESET: ClassVar[str] = "\033[0m"
    DIM: ClassVar[str] = "\033[2m"
    GRAY: ClassVar[str] = "\033[90m"
    WHITE_BOLD: ClassVar[str] = "\033[97;1m"

    def __init__(self, fmt: str | None = None, datefmt: str | None = None, use_color: bool = True):
        super().__init__(fmt=fmt, datefmt=datefmt)
        self.use_color = use_color

    def format(self, record: logging.LogRecord) -> str:
        timestamp = self.formatTime(record, "%H:%M:%S")
        name = record.name
        if name.startswith("usmp."):
            name = name[5:]
        elif name == "usmp":
            name = "core"

        msg = record.getMessage()

        if not self.use_color:
            level_name = record.levelname
            formatted = f"{timestamp} [{level_name:<5}] [{name}] {msg}"
        else:
            badge = self.LEVEL_BADGES.get(
                record.levelno,
                f"{self.GRAY}[{record.levelname:<5}]{self.RESET}",
            )
            # Format: 12:00:00  INFO   [server] Message
            formatted = (
                f"{self.GRAY}{timestamp}{self.RESET} "
                f"{badge} "
                f"{self.GRAY}[{self.WHITE_BOLD}{name}{self.GRAY}]{self.RESET} "
                f"{msg}"
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

    # Ensure stream supports UTF-8 on Windows without crashing on unicode characters
    if hasattr(stream, "reconfigure"):
        try:
            stream.reconfigure(encoding="utf-8", errors="replace")
        except (AttributeError, ValueError, OSError):
            logger.debug("Stream does not support reconfigure for UTF-8 encoding")

    if color is None:
        color = hasattr(stream, "isatty") and stream.isatty()

    handler = logging.StreamHandler(stream)
    handler.setLevel(level)
    handler.setFormatter(ColoredFormatter(fmt=fmt, use_color=color))
    logger.addHandler(handler)
    return handler
