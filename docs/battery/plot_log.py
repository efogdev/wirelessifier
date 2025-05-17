# Disclaimer
# This is coded by Claude, Deepseek and whatnot
# I have no fucking idea what's down below

import re
import matplotlib.pyplot as plt
from matplotlib.widgets import Button
import numpy as np
import sys
import argparse
import time
import os
from matplotlib.animation import FuncAnimation

class TimeUnitToggle:
    def __init__(self, fig, axes, raw_timestamps, bat_voltages, bat_corrected_voltages,
                 vin_voltages, events, monitor_mode=False):
        self.fig = fig
        self.axes = axes
        self.raw_timestamps = raw_timestamps
        self.bat_voltages = bat_voltages
        self.bat_corrected_voltages = bat_corrected_voltages
        self.vin_voltages = vin_voltages
        self.events = events
        self.boot_offsets = self.calculate_boot_offsets(raw_timestamps)
        self.monitor_mode = monitor_mode
        self.user_interaction = False
        self.last_data_length = len(raw_timestamps)
        self.event_lines = []
        self.setup_events()

        self.fig.canvas.mpl_connect('button_press_event', self.on_user_interaction)
        self.fig.canvas.mpl_connect('button_press_event', self.on_user_interaction)
        self.fig.canvas.mpl_connect('button_release_event', self.on_user_interaction)
        self.fig.canvas.mpl_connect('scroll_event', self.on_user_interaction)
        for ax in self.axes:
            ax.callbacks.connect('xlim_changed', self.on_user_interaction)
            ax.callbacks.connect('ylim_changed', self.on_user_interaction)

    def on_user_interaction(self, event):
        self.user_interaction = True

    def calculate_boot_offsets(self, timestamps):
        if not timestamps:
            return []

        offsets = [0]
        current_offset = 0

        for i in range(1, len(timestamps)):
            if timestamps[i] < timestamps[i-1]:
                current_offset += timestamps[i-1]
            offsets.append(current_offset)

        return offsets

    def get_converted_timestamps(self):
        converted = []
        for ts, offset in zip(self.raw_timestamps, self.boot_offsets):
            total_ms = ts + offset
            converted.append(total_ms / 60000)
        return converted

    def setup_events(self):
        self.event_lines = []
        self.event_texts = []
        event_colors = {
            "Timeout reached": "red",
            "Slow charging phase": "black",
            "Charging finished": "green"
        }

        for ax in self.axes:
            ax_lines = []
            ax_texts = []
            for event_time, event_name in self.events:
                event_offset = 0
                for i in range(1, len(self.boot_offsets)):
                    if event_time < self.raw_timestamps[i] and self.raw_timestamps[i] < self.raw_timestamps[i-1]:
                        event_offset = self.boot_offsets[i]
                        break

                total_ms = event_time + event_offset
                event_x = total_ms / 60000

                color = event_colors.get(event_name, "gray")
                line = ax.axvline(x=event_x, color=color, linestyle='--', alpha=0.7, label=f'event_{event_name}')
                text = ax.text(event_x, ax.get_ylim()[1]*0.5, event_name, rotation=90,
                        verticalalignment='top', color=color, label=f'event_text_{event_name}')
                ax_lines.append(line)
                ax_texts.append(text)
            self.event_lines.append(ax_lines)
            self.event_texts.append(ax_texts)

    def update_plots(self):
        if len(self.raw_timestamps) > self.last_data_length and self.monitor_mode:
            self.user_interaction = False
            self.last_data_length = len(self.raw_timestamps)

        x_values = self.get_converted_timestamps()
        x_label = 'Time (minutes)'

        current_xlims = [ax.get_xlim() for ax in self.axes]
        current_ylims = [ax.get_ylim() for ax in self.axes]

        for ax_idx, ax in enumerate(self.axes):
            # Update data lines
            for line in ax.lines:
                label = line.get_label()
                if label == 'Battery (ADC reading)':
                    line.set_data(x_values, self.bat_voltages)
                elif label == 'Battery (corrected)':
                    line.set_data(x_values, self.bat_corrected_voltages)
                elif label == 'USB input (Vin)':
                    line.set_data(x_values, self.vin_voltages)
                elif label.startswith('event_'):
                    # Skip event lines, they are handled separately
                    continue

            ax.set_xlabel(x_label)

            # Clear old events
            for line in self.event_lines[ax_idx]:
                line.remove()
            for text in self.event_texts[ax_idx]:
                text.remove()
            self.event_lines[ax_idx] = []
            self.event_texts[ax_idx] = []

            # Add events
            event_colors = {
                "Timeout reached": "red",
                "Slow charging phase": "black",
                "Charging finished": "green"
            }

            for event_time, event_name in self.events:
                event_offset = 0
                for j in range(1, len(self.boot_offsets)):
                    if event_time < self.raw_timestamps[j] and self.raw_timestamps[j] < self.raw_timestamps[j-1]:
                        event_offset = self.boot_offsets[j]
                        break

                total_ms = event_time + event_offset
                event_x = total_ms / 60000

                color = event_colors.get(event_name, "gray")
                line = ax.axvline(x=event_x, color=color, linestyle='--', alpha=0.7, label=f'event_{event_name}')
                text = ax.text(event_x, ax.get_ylim()[1]*0.5, event_name, rotation=90,
                        verticalalignment='top', color=color, label=f'event_text_{event_name}')
                self.event_lines[ax_idx].append(line)
                self.event_texts[ax_idx].append(text)

        if len(x_values) > 0:
            if self.monitor_mode and not self.user_interaction:
                latest_x = max(x_values)
                self.axes[0].set_xlim(min(x_values), latest_x)

                window = 1
                self.axes[1].set_xlim(max(latest_x - window, min(x_values)), latest_x)
            else:
                for ax, xlim, ylim in zip(self.axes, current_xlims, current_ylims):
                    if xlim != (0.0, 1.0) and self.user_interaction:
                        ax.set_xlim(xlim)
                    elif not self.user_interaction:
                        if ax == self.axes[0]:
                            ax.set_xlim(min(x_values), max(x_values))
                        else:
                            latest_x = max(x_values)
                            window = 1
                            ax.set_xlim(max(latest_x - window, min(x_values)), latest_x)

                    if ylim != (0.0, 1.0) and self.user_interaction:
                        ax.set_ylim(ylim)

def parse_log(filename):
    raw_timestamps = []
    bat_voltages = []
    bat_corrected_voltages = []
    vin_voltages = []
    events = []

    voltage_pattern = re.compile(r'I \((\d+)\) VMON: BAT: (\d+\.\d+)V(?:, BAT_corrected: (\d+\.\d+)V)?, Vin: (\d+\.\d+)V')
    timeout_pattern = re.compile(r'W \((\d+)\).*Timeout reached, terminating charging.*')
    slow_charging_pattern = re.compile(r'W \((\d+)\).*going into slow charging phase.*')
    finished_pattern = re.compile(r'W \((\d+)\).*Charging finished.*')

    with open(filename, 'r') as file:
        for line in file:
            match = voltage_pattern.search(line)
            if match:
                raw_timestamps.append(int(match.group(1)))
                bat_voltages.append(float(match.group(2)))
                # If BAT_corrected is present (group 3), use it, otherwise use BAT (group 2)
                if match.group(3):
                    bat_corrected_voltages.append(float(match.group(3)))
                else:
                    bat_corrected_voltages.append(float(match.group(2)))
                vin_voltages.append(float(match.group(4)))
                continue

            timeout_match = timeout_pattern.search(line)
            if timeout_match:
                events.append((int(timeout_match.group(1)), "Timeout reached"))
                continue

            slow_match = slow_charging_pattern.search(line)
            if slow_match:
                events.append((int(slow_match.group(1)), "Slow charging phase"))
                continue

            finished_match = finished_pattern.search(line)
            if finished_match:
                events.append((int(finished_match.group(1)), "Charging finished"))

    return raw_timestamps, bat_voltages, bat_corrected_voltages, vin_voltages, events

def plot_voltages(raw_timestamps, bat_voltages, bat_corrected_voltages, vin_voltages, events,
                  output_file=None, title=None, monitor=False):
    fig = plt.figure(figsize=(16, 8))
    axes = []

    boot_offsets = [0]
    current_offset = 0
    for i in range(1, len(raw_timestamps)):
        if raw_timestamps[i] < raw_timestamps[i-1]:
            current_offset += raw_timestamps[i-1]
        boot_offsets.append(current_offset)

    timestamps_min = [(ts + offset)/60000 for ts, offset in zip(raw_timestamps, boot_offsets)]

    ax1 = plt.subplot(2, 1, 1)
    line_bat, = plt.plot(timestamps_min, bat_voltages, 'b-', label='Battery (ADC reading)')
    line_vin, = plt.plot(timestamps_min, vin_voltages, 'r-', label='USB input (Vin)')
    # Only show corrected battery line if there are actual corrected values (not just copied from BAT)
    if any(bat != corr for bat, corr in zip(bat_voltages, bat_corrected_voltages)):
        line_corr, = plt.plot(timestamps_min, bat_corrected_voltages, 'g-', label='Battery (corrected)')

    plt.xlabel('Time (minutes)')
    plt.ylabel('Voltage (V)')
    plt.title(title if title else 'All Measurements')
    plt.grid(True)
    plt.legend()
    axes.append(ax1)

    ax2 = plt.subplot(2, 1, 2)
    line_bat2, = plt.plot(timestamps_min, bat_voltages, 'b-', label='Battery (ADC reading)')
    # Only show corrected battery line if there are actual corrected values (not just copied from BAT)
    if any(bat != corr for bat, corr in zip(bat_voltages, bat_corrected_voltages)):
        line_corr2, = plt.plot(timestamps_min, bat_corrected_voltages, 'g-', label='Battery (corrected)')

    plt.xlabel('Time (minutes)')
    plt.ylabel('Voltage (V)')
    plt.title('Battery (Last 1 minute)' if monitor else 'Battery (Zoomed)')
    plt.ylim(
        min(min(bat_voltages), min(bat_corrected_voltages)) - 0.01,
        max(max(bat_voltages), max(bat_corrected_voltages)) + 0.01
    )

    if monitor and timestamps_min:
        latest_x = max(timestamps_min)
        plt.xlim(max(latest_x - 1, min(timestamps_min)), latest_x)

    plt.grid(True)
    plt.legend()
    plt.tight_layout()
    axes.append(ax2)

    toggle = TimeUnitToggle(fig, axes, raw_timestamps, bat_voltages,
                            bat_corrected_voltages, vin_voltages, events, monitor)

    if output_file:
        plt.savefig(output_file)

    if monitor:
        monitor_state = {
            'fig': fig,
            'axes': axes,
            'toggle': toggle,
            'last_size': 0
        }

        def update_monitor(frame):
            try:
                current_size = os.path.getsize(args.log_file)
                if current_size == monitor_state['last_size']:
                    return
                monitor_state['last_size'] = current_size

                new_raw, new_bat, new_bat_corr, new_vin, new_events = parse_log(args.log_file)

                if len(new_raw) > len(monitor_state['toggle'].raw_timestamps):
                    monitor_state['toggle'].raw_timestamps = new_raw
                    monitor_state['toggle'].bat_voltages = new_bat
                    monitor_state['toggle'].bat_corrected_voltages = new_bat_corr
                    monitor_state['toggle'].vin_voltages = new_vin
                    # Clear old events
                    for ax_lines in monitor_state['toggle'].event_lines:
                        for line in ax_lines:
                            line.remove()
                    for ax_texts in monitor_state['toggle'].event_texts:
                        for text in ax_texts:
                            text.remove()
                    # Set new events and recreate event lines/texts
                    monitor_state['toggle'].events = new_events
                    monitor_state['toggle'].boot_offsets = monitor_state['toggle'].calculate_boot_offsets(new_raw)
                    monitor_state['toggle'].setup_events()

                    monitor_state['toggle'].update_plots()
                    fig.canvas.draw_idle()

            except Exception as e:
                print(f"Monitoring error: {e}")

        import os
        monitor_state['last_size'] = os.path.getsize(args.log_file)
        ani = FuncAnimation(fig, update_monitor, interval=1000)

    plt.show()

def main():
    parser = argparse.ArgumentParser(description='Parse and plot voltage data from log file')
    parser.add_argument('log_file', help='Path to the log file')
    parser.add_argument('-o', '--output', help='Output PNG file name (optional)')
    parser.add_argument('-t', '--title', help='Custom title for the plot (optional)')
    parser.add_argument('-m', '--monitor', action='store_true', help='Monitor the log file in real-time')

    global args
    args = parser.parse_args()

    raw_timestamps, bat_voltages, bat_corrected_voltages, vin_voltages, events = parse_log(args.log_file)

    boot_offsets = [0]
    current_offset = 0
    for i in range(1, len(raw_timestamps)):
        if raw_timestamps[i] < raw_timestamps[i-1]:
            current_offset += raw_timestamps[i-1]
        boot_offsets.append(current_offset)

    total_ms = raw_timestamps[-1] + boot_offsets[-1] if raw_timestamps else 0

    print(f"Number of data points: {len(raw_timestamps)}")
    if raw_timestamps:
        print(f"Total time: {total_ms/60000:.2f} minutes ({total_ms/1000:.2f} seconds)")
    print(f"Battery voltage range: {min(bat_voltages):.3f}V to {max(bat_voltages):.3f}V")
    print(f"Corrected battery voltage range: {min(bat_corrected_voltages):.3f}V to {max(bat_corrected_voltages):.3f}V")
    print(f"Input voltage range: {min(vin_voltages):.3f}V to {max(vin_voltages):.3f}V")

    if events:
        print("\nDetected events:")
        for time, name in events:
            event_offset = 0
            for i in range(1, len(boot_offsets)):
                if time < raw_timestamps[i] and raw_timestamps[i] < raw_timestamps[i-1]:
                    event_offset = boot_offsets[i]
                    break
            print(f"  {(time + event_offset)/60000:.2f} minutes: {name}")
    else:
        print("\nNo events detected")

    plot_voltages(raw_timestamps, bat_voltages, bat_corrected_voltages, vin_voltages, events,
                  args.output, args.title, args.monitor)

if __name__ == "__main__":
    main()
