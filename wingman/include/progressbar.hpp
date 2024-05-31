// The MIT License (MIT)
//
// Copyright (c) 2019 Luigi Pertoldi
// Copyright (c) 2024 Electric Pipelines, author: Curtis Gray
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to
// deal in the Software without restriction, including without limitation the
// rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
// sell copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
// FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
// IN THE SOFTWARE.
//
// ============================================================================
//  ___   ___   ___   __    ___   ____  __   __   ___    __    ___
// | |_) | |_) / / \ / /`_ | |_) | |_  ( (` ( (` | |_)  / /\  | |_)
// |_|   |_| \ \_\_/ \_\_/ |_| \ |_|__ _)_) _)_) |_|_) /_/--\ |_| \_
//
// Very simple progress bar for c++ loops with internal running variable
//
// Author: Luigi Pertoldi
// Created: 3 dic 2016
//
// Modified by: Curtis Gray
// Modified: 2024
//
// Notes: The bar must be used when there's no other possible source of output
//        inside the for loop
//

#pragma once

#include <iostream>
#include <ostream>
#include <string>
#include <stdexcept>
#include <chrono>

class progressbar {

public:
	// default destructor
	~progressbar() = default;

	// delete everything else
	progressbar(progressbar const &) = delete;
	progressbar &operator=(progressbar const &) = delete;
	progressbar(progressbar &&) = delete;
	progressbar &operator=(progressbar &&) = delete;

	// default constructor, must call set_niter later
	inline progressbar();
	inline progressbar(int n, bool showbar = true, std::ostream &out = std::cerr);

	// reset bar to use it again
	inline void reset();
	// set number of loop iterations
	inline void set_total(int iter);
	// update the bar with optional count and message
	inline void update(int count = 1, const std::string &message = "");
	// chose your style
	inline void set_done_char(const std::string &sym)
	{
		done_char = sym;
	}
	inline void set_todo_char(const std::string &sym)
	{
		todo_char = sym;
	}
	inline void set_opening_bracket_char(const std::string &sym)
	{
		opening_bracket_char = sym;
	}
	inline void set_closing_bracket_char(const std::string &sym)
	{
		closing_bracket_char = sym;
	}
	// to show only the percentage
	inline void show_bar(bool flag = true)
	{
		do_show_bar = flag;
	}
	// set the output stream
	inline void set_output_stream(const std::ostream &stream)
	{
		output.rdbuf(stream.rdbuf());
	}

private:
	int progress;
	int n_cycles;
	int last_perc;
	bool do_show_bar;
	bool update_is_called;

	std::string done_char;
	std::string todo_char;
	std::string opening_bracket_char;
	std::string closing_bracket_char;

	std::ostream &output;

	std::chrono::steady_clock::time_point start_time;
	std::chrono::milliseconds total_elapsed_time;
	std::chrono::milliseconds last_update_time;

	// Calculate and format time for display
	inline std::string format_time(std::chrono::milliseconds ms) const;
};

inline progressbar::progressbar() :
	progress(0),
	n_cycles(0),
	last_perc(0),
	do_show_bar(true),
	update_is_called(false),
	done_char("#"),
	todo_char(" "),
	opening_bracket_char("["),
	closing_bracket_char("]"),
	output(std::cerr)
{}

inline progressbar::progressbar(int n, bool showbar, std::ostream &out) :
	progress(0),
	n_cycles(n),
	last_perc(0),
	do_show_bar(showbar),
	update_is_called(false),
	done_char("#"),
	todo_char(" "),
	opening_bracket_char("["),
	closing_bracket_char("]"),
	output(out)
{}

inline void progressbar::reset()
{
	progress = 0;
	update_is_called = false;
	last_perc = 0;
}

inline void progressbar::set_total(int niter)
{
	if (niter <= 0) throw std::invalid_argument(
		"progressbar::set_niter: number of iterations null or negative");
	n_cycles = niter;
}


inline void progressbar::update(int count, const std::string &message)
{
	if (n_cycles == 0) {
		throw std::runtime_error("progressbar::update: number of cycles not set");
	}

	progress += count;

	if (!update_is_called) {
		start_time = std::chrono::steady_clock::now();
		total_elapsed_time = std::chrono::milliseconds(0);
		last_update_time = std::chrono::milliseconds(0);

		if (do_show_bar) {
			output << opening_bracket_char;
			for (int _ = 0; _ < 50; _++) {
				output << todo_char;
			}
			output << closing_bracket_char << " 0% (00:00:00) " << message;
		} else {
			output << "0% (00:00:00) " << message;
		}
	}
	update_is_called = true;

	int perc = progress * 100 / n_cycles;
	if (perc < last_perc) {
		return;
	}

	auto current_time = std::chrono::steady_clock::now();
	total_elapsed_time = std::chrono::duration_cast<std::chrono::milliseconds>(current_time - start_time);

	// Only update time display every 500 milliseconds (adjust as needed)
	if (total_elapsed_time - last_update_time >= std::chrono::milliseconds(500) || perc == 100) {
		last_update_time = total_elapsed_time;

		// Calculate average time per iteration
		std::chrono::milliseconds avg_time_per_iter = total_elapsed_time / progress;

		// Estimate time remaining
		std::chrono::milliseconds time_remaining = avg_time_per_iter * (n_cycles - progress);

		std::string elapsed_str = format_time(total_elapsed_time);
		std::string remaining_str = format_time(time_remaining);

		std::string progress_bar_str;
		if (do_show_bar) {
			progress_bar_str = opening_bracket_char;
			// Calculate the number of characters to fill
			int filled_chars = static_cast<int>(perc / 2.0); // 50 characters represent 100%

			// Draw the progress bar
			for (int i = 0; i < 50; ++i) {
				if (i < filled_chars) {
					progress_bar_str += done_char;
				} else {
					// Unfilled part of the bar
					progress_bar_str += todo_char;
				}
			}

			progress_bar_str += fmt::format("{} {:3}% ({}) {}",
										   closing_bracket_char, perc, remaining_str, message);
		} else {
			progress_bar_str = fmt::format("{:3}% ({}) {}", perc, remaining_str, message);
		}

		// Clear the line with spaces
		output << "\r" << std::string(progress_bar_str.length(), ' ');

		// Redraw the progress bar
		output << "\r" << progress_bar_str;
	}

	last_perc = perc;
	output << std::flush;
}

inline std::string progressbar::format_time(std::chrono::milliseconds ms) const
{
	std::chrono::seconds s = std::chrono::duration_cast<std::chrono::seconds>(ms);
	std::chrono::minutes m = std::chrono::duration_cast<std::chrono::minutes>(s);
	std::chrono::hours h = std::chrono::duration_cast<std::chrono::hours>(s);

	return fmt::format("{:02}:{:02}:{:02}", h.count(), m.count() % 60, s.count() % 60);
}
