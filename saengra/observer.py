from typing import TypeAlias, Protocol, Any

from saengra.api import Observer as APIObserver, ObservationType
from saengra.graph import Primitive


def observer(
    expr: str, *placeholder_values: Primitive, dont_register: bool = False
) -> "Observer":
    return Observer(expr, placeholder_values, dont_register=dont_register)


def collect_all(*modules: Any) -> list["Observer"]:
    collected_observers: set[Observer] = set()
    for module in modules:
        for value in module.__dict__.values():
            observers = getattr(value, _OBSERVERS_ATTR_NAME, [])
            collected_observers.update(observers)
    return _deduplicate_observers(collected_observers)


def _deduplicate_observers(observers: set["Observer"]) -> list["Observer"]:
    # It is very unlikely, but possible, that the same expression (with the same placeholders)
    # is watched more than once. That would lead to computational overhead, as there will be
    # more than one corresponding incremental matcher. The situation might also be surprising
    # for debug, so, since this is only invoked during the creation of a graph, we do the work.
    deduplicated_observers: dict[tuple[str, tuple[Primitive, ...]], Observer] = {}

    used_ids: set[str] = set()

    def compose_id(expr: str) -> str:
        # TODO maybe replace question marks with placeholder value reprs
        id_ = base = _generate_id_from_expression(expr)
        i = 1
        while id_ in used_ids:
            i += 1
            id_ = f"#{i}: {base}"
        used_ids.add(id_)
        return id_

    for w in observers:
        key = (w.expression, w.placeholder_values)
        if key not in deduplicated_observers:
            id_ = compose_id(w.expression)
            deduplicated_observers[key] = Observer(
                w.expression, w.placeholder_values
            ).with_id(id_)
        dw = deduplicated_observers[key]
        dw.create_handlers.extend(w.create_handlers)
        dw.change_handlers.extend(w.change_handlers)
        dw.delete_handlers.extend(w.delete_handlers)

    return [
        dw
        for dw in deduplicated_observers.values()
        if dw.create_handlers or dw.change_handlers or dw.delete_handlers
    ]


class RefsHandler(Protocol):
    @property
    def __name__(self) -> str:
        pass

    def __call__(self, *args, **kwargs) -> None:
        pass


CreateHandler: TypeAlias = RefsHandler
DeleteHandler: TypeAlias = RefsHandler
ChangeHandler: TypeAlias = RefsHandler


class Observer:
    def __init__(
        self,
        expr: str,
        placeholder_values: tuple[Primitive, ...],
        *,
        dont_register: bool = False,
    ) -> None:
        self.id = _generate_id_from_expression(expr)
        self.expression = expr
        self.placeholder_values = placeholder_values
        self.create_handlers: list[CreateHandler] = []
        self.change_handlers: list[ChangeHandler] = []
        self.delete_handlers: list[DeleteHandler] = []
        self.dont_register = dont_register

    def with_id(self, id_: str) -> "Observer":
        self.id = id_
        return self

    def on_create(self, handler: CreateHandler) -> CreateHandler:
        self.create_handlers.append(handler)
        if self.dont_register:
            return handler
        if getattr(handler, _OBSERVERS_ATTR_NAME, None) is None:
            handler.__observers__ = []  # type: ignore
        if self not in handler.__observers__:  # type: ignore
            handler.__observers__.append(self)  # type: ignore
        return handler

    def on_change(self, handler: ChangeHandler) -> ChangeHandler:
        self.change_handlers.append(handler)
        if self.dont_register:
            return handler
        if getattr(handler, _OBSERVERS_ATTR_NAME, None) is None:
            handler.__observers__ = []  # type: ignore
        if self not in handler.__observers__:  # type: ignore
            handler.__observers__.append(self)  # type: ignore
        return handler

    def on_create_or_change(self, handler: RefsHandler) -> RefsHandler:
        handler = self.on_create(handler)
        handler = self.on_change(handler)
        return handler

    def on_delete(self, handler: DeleteHandler) -> DeleteHandler:
        self.delete_handlers.append(handler)
        if self.dont_register:
            return handler
        if getattr(handler, _OBSERVERS_ATTR_NAME, None) is None:
            handler.__observers__ = []  # type: ignore
        if self not in handler.__observers__:  # type: ignore
            handler.__observers__.append(self)  # type: ignore
        return handler

    def to_api(self) -> APIObserver:
        return APIObserver(
            id=self.id,
            expression=self.expression,
            placeholder_values=self.placeholder_values,
            on_create=bool(self.on_create),
            on_change=bool(self.on_change),
            on_delete=bool(self.on_delete),
        )

    def pick_handlers(self, observation_type: ObservationType) -> list[RefsHandler]:
        match observation_type:
            case ObservationType.ON_CREATE:
                return self.create_handlers
            case ObservationType.ON_CHANGE:
                return self.change_handlers
            case ObservationType.ON_DELETE:
                return self.delete_handlers
            case _:
                raise ValueError(f"unknown observation type: {observation_type!r}")


def _generate_id_from_expression(expr: str) -> str:
    expr = expr.replace("--", "-")
    expr = expr.replace("<-", "<")
    expr = expr.replace("->", ">")
    expr = expr.replace(" as ", "$as$")
    expr = " ".join(expr.split())
    expr = expr.replace(" )", ")")
    expr = expr.replace("$as$", " as ")
    return expr


_OBSERVERS_ATTR_NAME = "__observers__"
