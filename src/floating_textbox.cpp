/*
	Copyright (C) 2006 - 2025
	by Joerg Hinrichs <joerg.hinrichs@alice-dsl.de>
	Copyright (C) 2003 by David White <dave@whitevine.net>
	Part of the Battle for Wesnoth Project https://www.wesnoth.org/

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.
	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY.

	See the COPYING file for more details.
*/

#include "floating_textbox.hpp"

#include "display_chat_manager.hpp"
#include "floating_label.hpp"
#include "font/standard_colors.hpp"
#include "game_display.hpp"
#include "preferences/preferences.hpp"
#include "log.hpp"

#include <ctime>

static lg::log_domain log_display("display");
#define ERR_DP LOG_STREAM(err, log_display)

namespace gui{
	floating_textbox::floating_textbox() :
		box_(nullptr),
		check_(nullptr),
		mode_(TEXTBOX_NONE),
		label_string_(),
		label_(0),
		command_history_()
	{}

	void floating_textbox::close()
	{
		if(!active()) {
			return;
		}
		if(check_ != nullptr) {
			if(mode_ == TEXTBOX_MESSAGE) {
				prefs::get().set_message_private(check_->checked());
			}
		}
		box_.reset(nullptr);
		check_.reset(nullptr);
		font::remove_floating_label(label_);
		mode_ = TEXTBOX_NONE;
	}

	void floating_textbox::update_location(game_display& gui)
	{
		if (box_ == nullptr)
			return;

		const rect& area = gui.map_outside_area();

		const int border_size = 10;

		const int ypos = area.y+area.h-30 - (check_ != nullptr ? check_->height() + border_size : 0);

		if (label_ != 0)
			font::remove_floating_label(label_);

		font::floating_label flabel(label_string_);
		flabel.set_color(font::YELLOW_COLOR);
		flabel.set_position(area.x + border_size, ypos);
		flabel.set_alignment(font::LEFT_ALIGN);
		flabel.set_clip_rect(area);

		label_ = font::add_floating_label(flabel);

		if (label_ == 0)
			return;

		const rect& label_area = font::get_floating_label_rect(label_);
		const int textbox_width = area.w - label_area.w - border_size*3;

		if(textbox_width <= 0) {
			font::remove_floating_label(label_);
			return;
		}

		if(box_ != nullptr) {
			const rect rect {
				  area.x + label_area.w + border_size * 2
				, ypos
				, textbox_width
				, box_->height()
			};

			box_->set_location(rect);
		}

		if(check_ != nullptr) {
			check_->set_location(box_->location().x,box_->location().y + box_->location().h + border_size);
		}
	}

	void floating_textbox::show(gui::TEXTBOX_MODE mode, const std::string& label,
		const std::string& check_label, bool checked, game_display& gui)
	{
		close();

		label_string_ = label;
		mode_ = mode;

		if(!check_label.empty()) {
			check_.reset(new gui::button(check_label,gui::button::TYPE_CHECK));
			check_->set_check(checked);
		}


		box_.reset(new gui::textbox(100,"",true,256,font::SIZE_NORMAL,0.8,0.6));

		update_location(gui);
	}

	void floating_textbox::tab(const std::set<std::string>& dictionary)
	{
		if(active() == false) {
			return;
		}

		std::string text = box_->text();
		std::vector<std::string> matches(dictionary.begin(), dictionary.end());
		const bool line_start = utils::word_completion(text, matches);

		if (matches.empty()) return;
		if (matches.size() == 1 && mode_ == gui::TEXTBOX_MESSAGE) {
			text.append(line_start ? ": " : " ");
		} else if (matches.size() > 1) {
			std::string completion_list = utils::join(matches, " ");
			game_display::get_singleton()->get_chat_manager().add_chat_message(
				std::chrono::system_clock::now(), "", 0, completion_list, events::chat_handler::MESSAGE_PRIVATE, false);
		}
		box_->set_text(text);
	}

	void floating_textbox::memorize_command(const std::string& command)
	{
		if(command.empty()) {
			return;
		}

		auto prev = std::find(command_history_.begin(), command_history_.end(), command);

		if(prev != command_history_.end()) {
			command_history_.erase(prev);
		}
		command_history_.emplace_back(command);
	}
}
