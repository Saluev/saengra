from termcolor import colored


class LazyColoredText:
    __slots__ = ("_text", "_color", "_attrs")

    def __init__(
        self, text: object, color: str, attrs: list[str] | None = None
    ) -> None:
        self._text = text
        self._color = color
        self._attrs = attrs

    def __str__(self) -> str:
        return colored(
            str(self._text), self._color, attrs=self._attrs, force_color=True
        )


def blue(s: object) -> LazyColoredText:
    return LazyColoredText(s, "blue")


def cyan(s: object) -> LazyColoredText:
    return LazyColoredText(s, "cyan")


def green(s: object) -> LazyColoredText:
    return LazyColoredText(s, "green")


def light_green(s: object) -> LazyColoredText:
    return LazyColoredText(s, "light_green")


def magenta(s: object) -> LazyColoredText:
    return LazyColoredText(s, "magenta")


def red(s: object) -> LazyColoredText:
    return LazyColoredText(s, "red")


def bold_red(s: object) -> LazyColoredText:
    return LazyColoredText(s, "red", ["bold"])


def white(s: object) -> LazyColoredText:
    return LazyColoredText(s, "white")


def yellow(s: object) -> LazyColoredText:
    return LazyColoredText(s, "yellow")
