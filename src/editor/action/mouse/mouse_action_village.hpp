/*
	Copyright (C) 2008 - 2025
	by Fabian Mueller <fabianmueller5@gmx.de>
	Part of the Battle for Wesnoth Project https://www.wesnoth.org/

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.
	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY.

	See the COPYING file for more details.
*/

#pragma once

#include "editor/action/mouse/mouse_action.hpp"

class CKey;

namespace editor {

/**
 * Editor mouse action class.
 * Called by the editor tool to assign village ownership to defined sides.
 */
class mouse_action_village : public mouse_action
{
public:
	mouse_action_village(const CKey& key, empty_palette& palette)
	: mouse_action(palette, key)
	{
	}

	/**
	 * No action.
	 */
	std::unique_ptr<editor_action> click_left(editor_display& /*disp*/, int /*x*/, int /*y*/) override {return nullptr;}

	/**
	 * If clicked on a village hex field, assigns the ownership of it to the current side.
	 */
	std::unique_ptr<editor_action> up_left(editor_display& disp, int x, int y) override;

	/**
	 * No action.
	 */
	std::unique_ptr<editor_action> click_right(editor_display& /*disp*/, int /*x*/, int /*y*/) override {return nullptr;}

	/**
	 * If clicked on a village hex field, unassigns it's ownership.
	 */
	std::unique_ptr<editor_action> up_right(editor_display& disp, int x, int y) override;

	virtual void set_mouse_overlay(editor_display& disp) override;
};


} //end namespace editor
