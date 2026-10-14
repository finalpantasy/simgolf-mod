"""Reject mismatched crash-fix anchors before any generated C is published."""


def validate_required_patches(functions, requirements, patches):
    by_name = {f["name"]: f for f in functions}
    errors = []
    for name, anchors in requirements.items():
        fn = by_name.get(name)
        if not fn or "c" not in fn:
            errors.append(f"{name}: required function body missing")
            continue
        configured = {old for old, _ in patches.get(name, [])}
        for old, expected in anchors:
            if old not in configured:
                errors.append(f"{name}: required anchor has no replacement: {old!r}")
            actual = fn["c"].count(old)
            if actual != expected:
                errors.append(f"{name}: required anchor expected {expected}, found {actual}: {old!r}")
    if errors:
        raise ValueError("required translator patches failed:\n" + "\n".join(errors))
