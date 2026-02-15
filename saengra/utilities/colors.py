from termcolor import colored


def blue(s: str) -> str:
    return colored(s, "blue", force_color=True)


def cyan(s: str) -> str:
    return colored(s, "cyan", force_color=True)


def green(s: str) -> str:
    return colored(s, "green", force_color=True)


def light_green(s: str) -> str:
    return colored(s, "light_green", force_color=True)


def magenta(s: str) -> str:
    return colored(s, "magenta", force_color=True)


def red(s: str) -> str:
    return colored(s, "red", force_color=True)


def bold_red(s: str) -> str:
    return colored(s, "red", attrs=["bold"], force_color=True)


def white(s: str) -> str:
    return colored(s, "white", force_color=True)


def yellow(s: str) -> str:
    return colored(s, "yellow", force_color=True)
