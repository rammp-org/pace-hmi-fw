"""The Jetson's ROS 2 stack for the stress bench: Guo's orchestrator and its mocks
(rammp-orchestrator#1), one OS process (one DDS participant) per node, as on the robot.

Unlike rammp_orchestrator's demo launch files this one does not force
RMW_IMPLEMENTATION: the bench runs Cyclone DDS like the robot, or Fast DDS to compare.

    ros2 launch jetson_mocks.launch.py            # 10 processes, the UI mock driving
    ros2 launch jetson_mocks.launch.py run_ui:=false
"""

from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument
from launch.conditions import IfCondition
from launch.substitutions import LaunchConfiguration
from launch_ros.actions import Node

ENGINES = ["articulation", "base_navigation", "pick_place", "microwave_controls", "feeding"]


def generate_launch_description() -> LaunchDescription:
    pkg = "rammp_orchestrator"
    nodes = [
        DeclareLaunchArgument("run_ui", default_value="true",
                              description="Also run the mock UI node, which drives the demo"),
        Node(package=pkg, executable="orchestrator_node", name="rammp_orchestrator", output="log"),
        Node(package=pkg, executable="perception_node", name="perception", output="log"),
        Node(package=pkg, executable="mock_arm_driver_node", name="mock_arm_driver", output="log"),
        Node(package=pkg, executable="mock_base_driver_node", name="mock_base_driver", output="log"),
        Node(package=pkg, executable="ui_node", name="ui", output="log",
             condition=IfCondition(LaunchConfiguration("run_ui"))),
    ]
    nodes += [Node(package=pkg, executable="engine_node", name=engine,
                   parameters=[{"engine": engine}], output="log") for engine in ENGINES]
    return LaunchDescription(nodes)
