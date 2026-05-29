"""Smoke test for the `ch_router` re-export package.

The wheel ships three packages: fastmm, routingkit_ch, ch_router. The other
test files import from the first two directly, so a packaging regression in
ch_router (the user-facing entrypoint) would ship green. This file imports the
public API surface and asserts every name in __all__ resolves to a real object.
"""

import ch_router


def test_all_exports_resolve():
    assert hasattr(ch_router, "__all__")
    for name in ch_router.__all__:
        assert hasattr(ch_router, name), f"ch_router.{name} declared in __all__ but missing"
        assert getattr(ch_router, name) is not None


def test_version_is_string():
    assert isinstance(ch_router.__version__, str)
    assert ch_router.__version__ != ""


def test_classes_are_same_objects_as_underlying_packages():
    """ch_router must re-export the actual C++-bound classes, not copies."""
    import fastmm
    import routingkit_ch

    assert ch_router.FastMapMatch is fastmm.FastMapMatch
    assert ch_router.UBODT is fastmm.UBODT
    assert ch_router.UBODTGenAlgorithm is fastmm.UBODTGenAlgorithm
    assert ch_router.ContractionHierarchy is routingkit_ch.ContractionHierarchy
    assert ch_router.CHQuery is routingkit_ch.CHQuery
