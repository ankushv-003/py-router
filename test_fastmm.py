"""
Comprehensive tests for fastmm Python bindings.

Run with: pytest test_py -v
Or: python test_py
"""

import sys
from pathlib import Path

# For running without installation
sys.path.insert(0, "build/python/pybind11/Release")

import pytest
from fastmm import FastMapMatch, MatchErrorCode, Network, NetworkGraph, Trajectory, TransitionMode


class TestNetworkBasics:
    """Test basic network creation and operations."""

    def test_create_empty_network(self):
        """Test creating an empty network."""
        network = Network()
        assert network.get_edge_count() == 0
        assert network.get_node_count() == 0

    def test_add_edge_with_coordinates(self):
        """Test adding edges using coordinate lists."""
        network = Network()
        network.add_edge(1, source=1, target=2, geom=[(0, 0), (100, 0)])
        assert network.get_edge_count() == 1
        assert network.get_node_count() == 2

    def test_add_edge_with_speed(self):
        """Test adding edges with speed values."""
        network = Network()
        network.add_edge(1, source=1, target=2, geom=[(0, 0), (100, 0)], speed=50)
        assert network.get_edge_count() == 1

    def test_add_multiple_edges(self):
        """Test adding multiple edges to build a network."""
        network = Network()
        network.add_edge(1, source=1, target=2, geom=[(0, 0), (100, 0)])
        network.add_edge(2, source=2, target=3, geom=[(100, 0), (200, 0)])
        network.add_edge(3, source=2, target=4, geom=[(100, 0), (100, 100)])
        assert network.get_edge_count() == 3
        assert network.get_node_count() == 4  # Nodes: 1, 2, 3, 4

    def test_add_edge_minimum_points(self):
        """Test that edges require at least 2 points."""
        network = Network()
        with pytest.raises(RuntimeError, match="at least 2 points"):
            network.add_edge(1, source=1, target=2, geom=[(0, 0)])

    def test_add_edge_empty_geometry(self):
        """Test that edges require non-empty geometry."""
        network = Network()
        with pytest.raises(RuntimeError, match="at least 2 points"):
            network.add_edge(1, source=1, target=2, geom=[])

    def test_add_edge_invalid_coordinate_format(self):
        """Test that coordinates must be tuples of (x, y)."""
        network = Network()
        with pytest.raises(RuntimeError, match="must be a tuple"):
            network.add_edge(1, source=1, target=2, geom=[(0, 0, 0)])  # 3D not allowed

    def test_build_rtree_on_empty_network(self):
        """Test that building rtree on empty network fails."""
        network = Network()
        with pytest.raises(RuntimeError, match="empty network"):
            network.finalize()

    def test_build_rtree_success(self):
        """Test successful rtree index building."""
        network = Network()
        network.add_edge(1, source=1, target=2, geom=[(0, 0), (100, 0)])
        network.finalize()  # Should not raise


class TestRtreeIndexRequired:
    """Test that operations fail when rtree index is not built."""

    def test_matching_without_rtree_fails(self):
        """Test that matching fails if finalize() is not called."""
        network = Network()
        network.add_edge(1, source=1, target=2, geom=[(0, 0), (100, 0)], speed=50)
        # Don't call finalize()

        # This should raise because rtree index was not built
        with pytest.raises(ValueError, match="must be finalized"):
            _ = FastMapMatch(network, TransitionMode.SHORTEST, max_distance_between_candidates=1000, cache_dir=".cache")


class TestTrajectoryCreation:
    """Test trajectory creation and manipulation."""

    def test_create_trajectory_from_xy(self):
        """Test creating trajectory from (x, y) tuples."""
        traj = Trajectory.from_xy_tuples([(0, 0), (100, 0), (200, 0)])
        assert len(traj) == 3

    def test_create_trajectory_from_xyt(self):
        """Test creating trajectory from (x, y, t) tuples."""
        traj = Trajectory.from_xyt_tuples([(0, 0, 0), (100, 0, 10), (200, 0, 20)])
        assert len(traj) == 3

    def test_trajectory_to_xy_tuples(self):
        """Test exporting trajectory to (x, y) tuples."""
        traj = Trajectory.from_xy_tuples([(0, 0), (100, 0)])
        xy_tuples = traj.to_xy_tuples()
        assert len(xy_tuples) == 2
        assert xy_tuples[0] == (0, 0)
        assert xy_tuples[1] == (100, 0)

    def test_trajectory_to_xyt_tuples(self):
        """Test exporting trajectory to (x, y, t) tuples."""
        traj = Trajectory.from_xyt_tuples([(0, 0, 5), (100, 0, 10)])
        xyt_tuples = traj.to_xyt_tuples()
        assert len(xyt_tuples) == 2
        assert xyt_tuples[0] == (0, 0, 5)
        assert xyt_tuples[1] == (100, 0, 10)

    def test_trajectory_with_decreasing_timestamps_fails(self):
        """Test that creating trajectory with non-increasing timestamps fails."""
        with pytest.raises(ValueError, match="non-decreasing"):
            Trajectory.from_xyt_tuples([(0, 0, 10), (100, 0, 5), (200, 0, 15)])

    def test_trajectory_with_equal_timestamps_succeeds(self):
        """Test that trajectory with equal consecutive timestamps is allowed."""
        # Non-decreasing means t[i] <= t[i+1], so equal timestamps are OK
        traj = Trajectory.from_xyt_tuples([(0, 0, 10), (100, 0, 10), (200, 0, 15)])
        assert len(traj) == 3


class TestNetworkGraph:
    """Test network graph creation with different routing modes."""

    def test_create_graph_shortest_mode(self):
        """Test creating graph with SHORTEST mode (default)."""
        network = Network()
        network.add_edge(1, source=1, target=2, geom=[(0, 0), (100, 0)])
        network.finalize()
        graph = NetworkGraph(network, TransitionMode.SHORTEST)
        assert graph is not None

    def test_create_graph_fastest_mode_with_speed(self):
        """Test creating graph with FASTEST mode when edges have speed."""
        network = Network()
        network.add_edge(1, source=1, target=2, geom=[(0, 0), (100, 0)], speed=50)
        network.add_edge(2, source=2, target=3, geom=[(100, 0), (200, 0)], speed=60)
        network.finalize()
        graph = NetworkGraph(network, TransitionMode.FASTEST)
        assert graph is not None

    def test_create_graph_fastest_mode_without_speed_fails(self):
        """Test that FASTEST mode fails if any edge lacks speed."""
        network = Network()
        network.add_edge(1, source=1, target=2, geom=[(0, 0), (100, 0)], speed=50)
        network.add_edge(2, source=2, target=3, geom=[(100, 0), (200, 0)])  # No speed
        network.finalize()
        with pytest.raises(ValueError, match="speed"):
            NetworkGraph(network, TransitionMode.FASTEST)


class TestShortestVsFastest:
    """Test the example from example_combined.py: shortest vs fastest routing."""

    @pytest.fixture
    def network_with_detour(self):
        """Create the network from example_combined.py with direct and detour routes."""
        network = Network()
        # Edge 1: A->B
        network.add_edge(1, source=1, target=2, geom=[(0, 0), (100, 0)], speed=50)
        # Edge 2: B->C direct (slower)
        network.add_edge(2, source=2, target=3, geom=[(100, 0), (200, 0)], speed=50)
        # Edge 3: B->up (faster detour)
        network.add_edge(3, source=2, target=4, geom=[(100, 0), (100, 10)], speed=100)
        # Edge 4: across top (faster detour)
        network.add_edge(4, source=4, target=5, geom=[(100, 10), (200, 10)], speed=100)
        # Edge 5: down->C (faster detour)
        network.add_edge(5, source=5, target=3, geom=[(200, 10), (200, 0)], speed=100)
        # Edge 6: C->D
        network.add_edge(6, source=3, target=6, geom=[(200, 0), (300, 0)], speed=50)
        network.finalize()
        return network

    def test_shortest_routing_prefers_direct_route(self, network_with_detour):
        """Test that SHORTEST routing prefers the direct route (Edge 2)."""
        network = network_with_detour
        matcher = FastMapMatch(
            network, TransitionMode.SHORTEST, max_distance_between_candidates=5000, cache_dir=".cache"
        )
        # Trajectory through A->B->?->C->D
        t = Trajectory.from_xyt_tuples([(25, 0, 0), (75, 0, 5), (150, 5, 10), (225, 0, 15), (275, 0, 20)])
        result = matcher.match(t, max_candidates=8, candidate_search_radius=15, gps_error=5)
        assert len(result.subtrajectories) == 1
        result = result.subtrajectories[0]
        assert result.error_code == MatchErrorCode.SUCCESS

        # Extract route
        all_edges = []
        for segment in result.segments:
            print(segment)
            for edge in segment.edges:
                all_edges.append(edge.edge_id)
                print(edge.points)

        # Remove consecutive duplicates
        route = []
        for edge_id in all_edges:
            if not route or route[-1] != edge_id:
                route.append(edge_id)

        # SHORTEST should prefer direct route: 1 -> 2 -> 6
        assert route == [1, 2, 6], f"Expected [1, 2, 6] but got {route}"

    def test_fastest_routing_prefers_detour_route(self, network_with_detour):
        """Test that FASTEST routing prefers the faster detour route (Edges 3->4->5)."""
        network = network_with_detour
        matcher = FastMapMatch(network, TransitionMode.FASTEST, max_time_between_candidates=5000, cache_dir=".cache")
        # Trajectory through A->B->?->C->D
        t = Trajectory.from_xyt_tuples([(25, 0, 0), (75, 0, 5), (150, 5, 10), (225, 0, 15), (275, 0, 20)])
        result = matcher.match(t, max_candidates=8, candidate_search_radius=15, gps_error=5, reference_speed=50)
        assert len(result.subtrajectories) == 1
        result = result.subtrajectories[0]
        assert result.error_code == MatchErrorCode.SUCCESS
        # Extract route
        all_edges = []
        for segment in result.segments:
            for edge in segment.edges:
                all_edges.append(edge.edge_id)

        # Remove consecutive duplicates
        route = []
        for edge_id in all_edges:
            if not route or route[-1] != edge_id:
                route.append(edge_id)

        # FASTEST should prefer detour route: 1 -> 3 -> 4 -> 5 -> 6
        assert route == [1, 3, 4, 5, 6], f"Expected [1, 3, 4, 5, 6] but got {route}"


class TestMatchResult:
    """Test match result structure and access."""

    def test_match_result_structure(self):
        """Test that match result has expected structure."""
        network = Network()
        network.add_edge(1, source=1, target=2, geom=[(0, 0), (100, 0)], speed=50)
        network.finalize()

        matcher = FastMapMatch(
            network,
            TransitionMode.SHORTEST,
            max_distance_between_candidates=1000,
            cache_dir=".cache/test_match_result_structure",
        )
        t = Trajectory.from_xy_tuples([(10, 0), (90, 0)])
        result = matcher.match(t, max_candidates=4, candidate_search_radius=50, gps_error=50)
        assert hasattr(result, "subtrajectories")

        # Check result structure
        result = result.subtrajectories[0]
        assert hasattr(result, "error_code")
        assert hasattr(result, "segments")
        assert hasattr(result, "start_index")
        assert hasattr(result, "end_index")

    def test_match_result_segments(self):
        """Test accessing match result segments and edges."""
        network = Network()
        network.add_edge(1, source=1, target=2, geom=[(0, 0), (100, 0)], speed=50)
        network.add_edge(2, source=2, target=3, geom=[(100, 0), (200, 0)], speed=50)
        network.finalize()

        matcher = FastMapMatch(
            network, TransitionMode.SHORTEST, max_distance_between_candidates=1000, cache_dir=".cache"
        )
        t = Trajectory.from_xy_tuples([(10, 0), (150, 0)])
        result = matcher.match(t, max_candidates=4, candidate_search_radius=50, gps_error=50)
        assert len(result.subtrajectories) == 1
        result = result.subtrajectories[0]
        assert result.error_code == MatchErrorCode.SUCCESS

        # Access segments
        for segment in result.segments:
            assert hasattr(segment, "p0")
            assert hasattr(segment, "p1")
            assert hasattr(segment, "edges")
            for edge in segment.edges:
                assert hasattr(edge, "edge_id")
                assert hasattr(edge, "points")


class TestSplitMatching:
    """Test automatic trajectory splitting on failures."""

    def test_split_match_basic(self):
        """Test basic split matching with continuous trajectory."""
        network = Network()
        network.add_edge(1, source=1, target=2, geom=[(0, 0), (100, 0)])
        network.add_edge(2, source=2, target=3, geom=[(100, 0), (200, 0)])
        network.finalize()

        matcher = FastMapMatch(
            network,
            TransitionMode.SHORTEST,
            max_distance_between_candidates=1000,
            cache_dir=".cache/test_split_match_result_structure",
        )
        # Simple trajectory that should match completely
        t = Trajectory.from_xy_tuples([(10, 0), (50, 0), (150, 0)])
        result = matcher.match(t, max_candidates=4, candidate_search_radius=50, gps_error=50)

        assert len(result.subtrajectories) >= 1
        # Should have at least one successful sub-trajectory
        success_count = sum(1 for sub in result.subtrajectories if sub.error_code == MatchErrorCode.SUCCESS)
        assert success_count >= 1

    def test_split_match_with_gap(self):
        """Test split matching with a point far from network (simulates failure)."""
        network = Network()
        network.add_edge(1, source=1, target=2, geom=[(0, 0), (100, 0)])
        network.add_edge(2, source=2, target=3, geom=[(100, 0), (200, 0)])
        network.finalize()

        matcher = FastMapMatch(
            network, TransitionMode.SHORTEST, max_distance_between_candidates=1000, cache_dir=".cache"
        )
        # Trajectory with point far from network
        t = Trajectory.from_xy_tuples(
            [
                (10, 0),  # Point 0 - near network
                (50, 0),  # Point 1 - near network
                (100, 500),  # Point 2 - far from network (should be skipped)
                (150, 0),  # Point 3 - near network
                (180, 0),  # Point 4 - near network
            ],
        )
        result = matcher.match(t, max_candidates=4, candidate_search_radius=30, gps_error=20)

        # Should have successfully matched portions (points 0-1 and 3-4)
        # Point 2 is just skipped (no single-point sub-trajectories added)
        assert len(result.subtrajectories) >= 1

        # All returned sub-trajectories should be successful
        for sub in result.subtrajectories:
            assert sub.error_code == MatchErrorCode.SUCCESS

        # Print results for debugging
        print(f"\nSplit match result: {len(result.subtrajectories)} sub-trajectories")
        for i, sub in enumerate(result.subtrajectories):
            print(f"  Sub {i}: points [{sub.start_index}-{sub.end_index}] with {len(sub.segments)} segments")

    def test_split_match_result_structure(self):
        """Test the structure of split match results."""
        network = Network()
        network.add_edge(1, source=1, target=2, geom=[(0, 0), (100, 0)])
        network.finalize()

        matcher = FastMapMatch(
            network, TransitionMode.SHORTEST, max_distance_between_candidates=1000, cache_dir=".cache"
        )
        t = Trajectory.from_xy_tuples([(10, 0), (50, 0)])
        result = matcher.match(t, max_candidates=4, candidate_search_radius=50, gps_error=50)

        # Check result structure
        assert hasattr(result, "subtrajectories")
        assert isinstance(result.subtrajectories, list)

        # Check sub-trajectory structure
        for sub in result.subtrajectories:
            assert hasattr(sub, "start_index")
            assert hasattr(sub, "end_index")
            assert hasattr(sub, "error_code")
            assert hasattr(sub, "segments")
            assert isinstance(sub.segments, list)

            # All returned sub-trajectories should be successful
            assert sub.error_code == MatchErrorCode.SUCCESS

            # For 2+ points, should have at least 1 segment
            if sub.end_index > sub.start_index:
                assert len(sub.segments) >= 1

    def test_split_match_disconnected_by_distance(self):
        """Test split matching when points are too far apart for UBODT (disconnected layers)."""
        # Create a long network
        network = Network()
        network.add_edge(1, source=1, target=2, geom=[(0, 0), (100, 0)])
        network.add_edge(2, source=2, target=3, geom=[(100, 0), (200, 0)])
        network.add_edge(3, source=3, target=4, geom=[(200, 0), (300, 0)])
        network.add_edge(4, source=4, target=5, geom=[(300, 0), (400, 0)])
        network.add_edge(5, source=5, target=6, geom=[(400, 0), (500, 0)])
        network.add_edge(6, source=6, target=7, geom=[(500, 0), (600, 0)])
        network.add_edge(7, source=7, target=8, geom=[(600, 0), (700, 0)])
        network.add_edge(8, source=8, target=9, geom=[(700, 0), (800, 0)])
        network.finalize()

        matcher = FastMapMatch(
            network, TransitionMode.SHORTEST, max_distance_between_candidates=250, cache_dir=".cache"
        )
        # Use max_candidates=1 and small radius to ensure we get the closest edge only
        # First, test with regular matching to see what happens
        t = Trajectory.from_xy_tuples(
            [
                (150, 0),  # Point on edge 2 (middle of 100-200)
                (750, 0),  # Point on edge 8 (middle of 700-800) - very far away
            ],
        )
        result = matcher.match(t, max_candidates=1, candidate_search_radius=30, gps_error=50)
        assert len(result.subtrajectories) == 0

        # Create trajectory with points that:
        # - All have candidates (near roads)
        # - But some consecutive points are beyond UBODT delta distance
        t = Trajectory.from_xy_tuples(
            [
                (50, 0),  # Point 0 - on edge 1 (middle of 0-100)
                (150, 0),  # Point 1 - on edge 2 (middle of 100-200)
                (750, 0),  # Point 2 - on edge 8 (middle of 700-800) - path requires >500 units
                (780, 0),  # Point 3 - also on edge 8
            ],
        )
        result = matcher.match(t, max_candidates=1, candidate_search_radius=30, gps_error=50)

        # Should split into at least 2 sub-trajectories due to disconnection
        # Point 1 (edge 2) to Point 2 (edge 7) requires path distance of ~400-500 units
        # which exceeds delta=250, so UBODT won't have this path precomputed
        # Expected: Points 0-1 match, then disconnect at 1->2, then points 2-3 match
        assert len(result.subtrajectories) >= 2

        # All returned sub-trajectories should be successful
        for sub in result.subtrajectories:
            assert sub.error_code == MatchErrorCode.SUCCESS

        # Print results for debugging
        print(f"\nDisconnected layers test: {len(result.subtrajectories)} sub-trajectories")
        for i, sub in enumerate(result.subtrajectories):
            num_points = sub.end_index - sub.start_index + 1
            print(
                f"  Sub {i}: points [{sub.start_index}-{sub.end_index}] ({num_points} points) with {len(sub.segments)} segments"
            )


class TestReversedGeometry:
    """Test handling of reversed geometry when GPS moves backward on same edge."""

    def test_fails_to_reverse_if_outside_tolerance(self):
        """Reverse tolerance of e.g. 10 units only allows reversing 10 units of edge length, so if reverse is 50
        units, it should fail."""
        network = Network()
        # Create a simple straight road
        network.add_edge(1, source=1, target=2, geom=[(0, 0), (100, 0)], speed=50)
        network.finalize()

        matcher = FastMapMatch(
            network,
            TransitionMode.SHORTEST,
            max_distance_between_candidates=1000,
            cache_dir=".cache/test_reversed",
        )

        # Allow reverse tolerance for backward movement
        # GPS points that move backward: 80m -> 30m on same edge
        t = Trajectory.from_xy_tuples([(80, 0), (30, 0)])

        result = matcher.match(
            t,
            max_candidates=4,
            candidate_search_radius=50,
            gps_error=50,
            reverse_tolerance=10,  # Only allow 10m backward
        )
        assert len(result.subtrajectories) == 0

    def test_reversed_flag_on_backward_movement(self):
        """Test that reversed flag is set when GPS moves backward on same edge."""
        network = Network()
        # Create a simple straight road
        network.add_edge(1, source=1, target=2, geom=[(0, 0), (100, 0)], speed=50)
        network.finalize()
        matcher = FastMapMatch(
            network,
            TransitionMode.SHORTEST,
            max_distance_between_candidates=1000,
            cache_dir=".cache/test_reversed",
        )

        # Allow reverse tolerance for backward movement
        # GPS points that move backward: 80m -> 30m on same edge
        t = Trajectory.from_xy_tuples([(80, 0), (30, 0)])
        result = matcher.match(
            t,
            max_candidates=4,
            candidate_search_radius=50,
            gps_error=50,
            reverse_tolerance=60,  # Allow 60m backward movement
        )
        assert len(result.subtrajectories) == 1
        result = result.subtrajectories[0]
        assert result.error_code == MatchErrorCode.SUCCESS

        # Should have one segment with one edge
        assert len(result.segments) == 1
        segment = result.segments[0]
        assert len(segment.edges) == 1
        edge = segment.edges[0]

        # The reversed flag should be set
        assert hasattr(edge, "reversed"), "Edge should have 'reversed' attribute"
        assert edge.reversed is True, "Edge should be marked as reversed"

        # Geometry should still go forward (from 30 to 80) after auto-correction
        # First point should be at lower offset, last at higher offset
        assert len(edge.points) >= 2
        # The edge_offset should increase from first to last point
        assert edge.points[0].edge_offset < edge.points[-1].edge_offset, (
            "Geometry should be corrected to go forward even when reversed"
        )

        print(f"\nReversed edge test: edge_id={edge.edge_id}, reversed={edge.reversed}")
        print(f"  First point offset: {edge.points[0].edge_offset:.1f}")
        print(f"  Last point offset: {edge.points[-1].edge_offset:.1f}")

    def test_not_reversed_on_forward_movement(self):
        """Test that reversed flag is False for normal forward movement."""
        network = Network()
        network.add_edge(1, source=1, target=2, geom=[(0, 0), (100, 0)], speed=50)
        network.finalize()

        matcher = FastMapMatch(
            network,
            TransitionMode.SHORTEST,
            max_distance_between_candidates=1000,
            cache_dir=".cache/test_not_reversed",
        )
        # Normal forward movement: 30m -> 80m
        t = Trajectory.from_xy_tuples([(30, 0), (80, 0)])
        result = matcher.match(t, max_candidates=4, candidate_search_radius=50, gps_error=50)
        assert len(result.subtrajectories) == 1
        result = result.subtrajectories[0]
        assert result.error_code == MatchErrorCode.SUCCESS

        segment = result.segments[0]
        edge = segment.edges[0]

        # Should NOT be marked as reversed
        assert edge.reversed is False, "Forward movement should not be marked as reversed"

        # Geometry should go forward
        assert edge.points[0].edge_offset < edge.points[-1].edge_offset

    def test_reversed_geometry_consistency(self):
        """Test that reversed geometry maintains spatial consistency."""
        network = Network()
        # Multi-segment linestring
        network.add_edge(1, source=1, target=2, geom=[(0, 0), (50, 0), (100, 0)], speed=50)
        network.finalize()

        matcher = FastMapMatch(
            network,
            TransitionMode.SHORTEST,
            max_distance_between_candidates=1000,
            cache_dir=".cache/test_reversed_consistency",
        )
        # Backward movement on multi-segment edge
        t = Trajectory.from_xy_tuples([(90, 0), (40, 0)])
        result = matcher.match(t, max_candidates=4, candidate_search_radius=50, gps_error=50, reverse_tolerance=60)
        assert len(result.subtrajectories) == 1

        result = result.subtrajectories[0]
        assert result.error_code == MatchErrorCode.SUCCESS

        edge = result.segments[0].edges[0]
        assert edge.reversed is True

        # Points should form a valid path with increasing offsets
        for i in range(len(edge.points) - 1):
            assert edge.points[i].edge_offset <= edge.points[i + 1].edge_offset, (
                f"Point {i} offset {edge.points[i].edge_offset} should be <= point {i + 1} offset {edge.points[i + 1].edge_offset}"
            )

        print(f"\nMulti-segment reversed edge: {len(edge.points)} points")
        print(f"  Offset range: {edge.points[0].edge_offset:.1f} to {edge.points[-1].edge_offset:.1f}")


class TestTimeInterpolationEdgeCases:
    """Test time interpolation with potential divide-by-zero edge cases."""

    def test_stationary_trajectory_time_interpolation(self):
        """Test time interpolation when GPS points are at the exact same location."""
        network = Network()
        network.add_edge(1, source=1, target=2, geom=[(0, 0), (100, 0)], speed=50)
        network.finalize()

        matcher = FastMapMatch(
            network,
            TransitionMode.SHORTEST,
            max_distance_between_candidates=1000,
            cache_dir=".cache/test_stationary_time",
        )
        # Two identical GPS points with different timestamps
        t = Trajectory.from_xyt_tuples([(50, 0, 100), (50, 0, 200)])
        result = matcher.match(t, max_candidates=4, candidate_search_radius=50, gps_error=50)

        assert len(result.subtrajectories) == 1
        sub = result.subtrajectories[0]
        assert sub.error_code == MatchErrorCode.SUCCESS

        # Check that timestamps are NOT nan
        import math

        for segment in sub.segments:
            for edge in segment.edges:
                for point in edge.points:
                    assert not math.isnan(point.t), f"Point at ({point.x}, {point.y}) has NaN timestamp"
                    assert point.t >= 100.0 and point.t <= 200.0

    def test_zero_speed_segment_time_interpolation(self):
        """Test time interpolation when edge speed is very low or distance is zero."""
        network = Network()
        # Edge with very low distance
        network.add_edge(1, source=1, target=2, geom=[(0, 0), (0.000001, 0)], speed=50)
        network.finalize()

        matcher = FastMapMatch(
            network,
            TransitionMode.FASTEST,
            max_time_between_candidates=1000,
            cache_dir=".cache/test_zero_dist_time",
        )
        # GPS points far outside the tiny edge, but will snap to it
        t = Trajectory.from_xyt_tuples([(10, 0, 10), (20, 0, 20)])
        result = matcher.match(t, max_candidates=4, candidate_search_radius=50, gps_error=50, reference_speed=50)

        assert len(result.subtrajectories) == 1
        sub = result.subtrajectories[0]
        assert sub.error_code == MatchErrorCode.SUCCESS

        # Check that timestamps are NOT nan
        import math

        for segment in sub.segments:
            for edge in segment.edges:
                for point in edge.points:
                    assert not math.isnan(point.t), f"Point at ({point.x}, {point.y}) has NaN timestamp"


if __name__ == "__main__":
    # Allow running without pytest
    print("Running tests...")
    pytest.main([__file__, "-v"])
