/*
	Copyright (C) 2008 - 2025
	by Mark de Wever <koraq@xs4all.nl>
	Part of the Battle for Wesnoth Project https://www.wesnoth.org/

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.
	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY.

	See the COPYING file for more details.
*/

#define GETTEXT_DOMAIN "wesnoth-lib"

#include "gui/widgets/grid_private.hpp"

#include "gui/auxiliary/iterator/walker_grid.hpp"
#include "gui/core/event/message.hpp"
#include "gui/core/log.hpp"
#include "gui/core/layout_exception.hpp"
#include "gui/widgets/styled_widget.hpp"
#include "gui/widgets/window.hpp"

#include <numeric>

#define LOG_SCOPE_HEADER "grid [" + id() + "] " + __func__
#define LOG_HEADER LOG_SCOPE_HEADER + ':'
#define LOG_IMPL_HEADER "grid [" + grid.id() + "] " + __func__ + ':'

#define LOG_CHILD_SCOPE_HEADER                                                 \
	"grid::child [" + (widget_ ? widget_->id() : "-") + "] " + __func__
#define LOG_CHILD_HEADER LOG_CHILD_SCOPE_HEADER + ':'

namespace gui2
{

grid::grid(const unsigned rows, const unsigned cols)
	: widget()
	, rows_(rows)
	, cols_(cols)
	, row_height_()
	, col_width_()
	, row_grow_factor_(rows)
	, col_grow_factor_(cols)
	, children_(rows * cols)
{
	connect_signal<event::REQUEST_PLACEMENT>(
		std::bind(&grid::request_placement, this,
			std::placeholders::_1, std::placeholders::_2, std::placeholders::_3, std::placeholders::_4),
		event::dispatcher::back_pre_child);
}

grid::~grid()
{
}

unsigned grid::add_row(const unsigned count)
{
	assert(count);

	// FIXME the warning in set_rows_cols should be killed.

	unsigned result = rows_;
	set_rows_cols(rows_ + count, cols_);
	return result;
}

void grid::set_child(std::unique_ptr<widget> widget,
					  const unsigned row,
					  const unsigned col,
					  const unsigned flags,
					  const unsigned border_size)
{
	assert(row < rows_ && col < cols_);
	assert(flags & VERTICAL_MASK);
	assert(flags & HORIZONTAL_MASK);

	child& cell = get_child(row, col);

	// clear old child if any
	if(cell.get_widget()) {
		// free a child when overwriting it
		WRN_GUI_G << LOG_HEADER << " child '" << cell.id() << "' at cell '"
				  << row << ',' << col << "' will be replaced.";
	}

	// copy data
	cell.set_flags(flags);
	cell.set_border_size(border_size);
	cell.set_widget(std::move(widget));

	// make sure the new child is valid before deferring
	if(gui2::widget* w = cell.get_widget()) {
		w->set_parent(this);
	}
}

std::unique_ptr<widget> grid::swap_child(const std::string& id,
		std::unique_ptr<widget> w,
		const bool recurse,
		widget* new_parent)
{
	assert(w);

	for(auto& child : children_) {
		if(child.id() != id) {
			if(recurse) {
				if(grid* g = dynamic_cast<grid*>(child.get_widget())) {
					// Save the current value of `w` before we recurse. If a match is found in
					// a child grid, the old widget will be returned and won't match.
					widget* current_w = w.get();

					if(auto res = g->swap_child(id, std::move(w), true); res.get() != current_w) {
						return res;
					} else {
						// Since `w` was moved "down" a level, it will be null once we return to
						// this invocation. Re-assign it so we don't crash below.
						w = std::move(res);
					}
				}
			}

			continue;
		}

		// Free widget from cell and validate.
		auto old = child.free_widget();
		assert(old);

		old->set_parent(new_parent);

		w->set_parent(this);
		w->set_visible(old->get_visible());

		child.set_widget(std::move(w));
		return old;
	}

	return w;
}

void grid::remove_child(const unsigned row, const unsigned col)
{
	assert(row < rows_ && col < cols_);

	child& cell = get_child(row, col);
	cell.set_widget(nullptr);
}

void grid::remove_child(const std::string& id, const bool find_all)
{
	for(auto & child : children_)
	{
		if(child.id() == id) {
			child.set_widget(nullptr);

			if(!find_all) {
				break;
			}
		}
	}
}

void grid::set_active(const bool active)
{
	for(auto & child : children_)
	{

		widget* widget = child.get_widget();
		if(!widget) {
			continue;
		}

		grid* g = dynamic_cast<grid*>(widget);
		if(g) {
			g->set_active(active);
			continue;
		}

		styled_widget* control = dynamic_cast<styled_widget*>(widget);
		if(control) {
			control->set_active(active);
		}
	}
}

void grid::layout_initialize(const bool full_initialization)
{
	// Inherited.
	widget::layout_initialize(full_initialization);

	// Clear child caches.
	for(auto & child : children_)
	{

		child.layout_initialize(full_initialization);
	}
}

void grid::reduce_width(const unsigned maximum_width)
{
	/***** ***** ***** ***** INIT ***** ***** ***** *****/
	log_scope2(log_gui_layout, LOG_SCOPE_HEADER);
	DBG_GUI_L << LOG_HEADER << " maximum width " << maximum_width << ".";

	point size = get_best_size();
	if(size.x <= static_cast<int>(maximum_width)) {
		DBG_GUI_L << LOG_HEADER << " Already fits.";
		return;
	}

	/***** ***** ***** ***** Request resize ***** ***** ***** *****/

	request_reduce_width(maximum_width);

	size = get_best_size();
	if(size.x <= static_cast<int>(maximum_width)) {
		DBG_GUI_L << LOG_HEADER << " Resize request honored.";
		return;
	}

	/***** ***** ***** ***** Demand resize ***** ***** ***** *****/

	/** @todo Implement. */

	/***** ***** ***** ***** Acknowledge failure ***** ***** ***** *****/

	DBG_GUI_L << LOG_HEADER << " Resizing failed.";

	throw layout_exception_width_resize_failed();
}

void grid::request_reduce_width(const unsigned maximum_width)
{
	point size = get_best_size();
	if(size.x <= static_cast<int>(maximum_width)) {
		/** @todo this point shouldn't be reached, find out why it does. */
		return;
	}

	const unsigned too_wide = size.x - maximum_width;
	unsigned reduced = 0;
	for(std::size_t col = 0; col < cols_; ++col) {
		if(too_wide - reduced >= col_width_[col]) {
			DBG_GUI_L << LOG_HEADER << " column " << col
					  << " is too small to be reduced.";
			continue;
		}

		const unsigned wanted_width = col_width_[col] - (too_wide - reduced);
		const unsigned width
				= grid_implementation::column_request_reduce_width(
						*this, col, wanted_width);

		if(width < col_width_[col]) {
			unsigned reduction = col_width_[col] - width;

			DBG_GUI_L << LOG_HEADER << " reduced " << reduction
					  << " pixels for column " << col << ".";

			size.x -= reduction;
			reduced += reduction;
		}

		if(size.x <= static_cast<int>(maximum_width)) {
			break;
		}
	}

	set_layout_size(calculate_best_size());
}

void grid::demand_reduce_width(const unsigned /*maximum_width*/)
{
	/** @todo Implement. */
}

void grid::reduce_height(const unsigned maximum_height)
{
	/***** ***** ***** ***** INIT ***** ***** ***** *****/
	log_scope2(log_gui_layout, LOG_SCOPE_HEADER);
	DBG_GUI_L << LOG_HEADER << " maximum height " << maximum_height << ".";

	point size = get_best_size();
	if(size.y <= static_cast<int>(maximum_height)) {
		DBG_GUI_L << LOG_HEADER << " Already fits.";
		return;
	}

	/***** ***** ***** ***** Request resize ***** ***** ***** *****/

	request_reduce_height(maximum_height);

	size = get_best_size();
	if(size.y <= static_cast<int>(maximum_height)) {
		DBG_GUI_L << LOG_HEADER << " Resize request honored.";
		return;
	}

	/***** ***** ***** ***** Demand resize ***** ***** ***** *****/

	/** @todo Implement. */

	/***** ***** ***** ***** Acknowledge failure ***** ***** ***** *****/

	DBG_GUI_L << LOG_HEADER << " Resizing failed.";

	throw layout_exception_height_resize_failed();
}

void grid::request_reduce_height(const unsigned maximum_height)
{
	point size = get_best_size();
	if(size.y <= static_cast<int>(maximum_height)) {
		/** @todo this point shouldn't be reached, find out why it does. */
		return;
	}

	const unsigned too_high = size.y - maximum_height;
	unsigned reduced = 0;
	for(std::size_t row = 0; row < rows_; ++row) {
		unsigned wanted_height = row_height_[row] - (too_high - reduced);
		/**
		 * @todo Improve this code.
		 *
		 * Now we try every item to be reduced, maybe items need a flag whether
		 * or not to try to reduce and also evaluate whether the force
		 * reduction is still needed.
		 */
		if(too_high - reduced >= row_height_[row]) {
			DBG_GUI_L << LOG_HEADER << " row " << row << " height "
					  << row_height_[row] << " want to reduce " << too_high
					  << " is too small to be reduced fully try 1 pixel.";

			wanted_height = 1;
		}

		/* Reducing the height of a widget causes the widget to save its new size
		in widget::layout_size_. After that, get_best_size() will return that
		size and not the originally calculated optimal size.
		Thus, it's perfectly correct that grid::calculate_best_size() that we
		call later calls get_best_size() for child widgets as if size reduction
		had never happened. */
		const unsigned height = grid_implementation::row_request_reduce_height(
				*this, row, wanted_height);

		if(height < row_height_[row]) {
			unsigned reduction = row_height_[row] - height;

			DBG_GUI_L << LOG_HEADER << " row " << row << " height "
					  << row_height_[row] << " want to reduce " << too_high
					  << " reduced " << reduction << " pixels.";

			size.y -= reduction;
			reduced += reduction;
		}

		if(size.y <= static_cast<int>(maximum_height)) {
			break;
		}
	}

	size = calculate_best_size();

	DBG_GUI_L << LOG_HEADER << " Requested maximum " << maximum_height
			  << " resulting height " << size.y << ".";

	set_layout_size(size);
}

void grid::demand_reduce_height(const unsigned /*maximum_height*/)
{
	/** @todo Implement. */
}

void grid::request_placement(dispatcher&, const event::ui_event, bool& handled, bool&)
{
	if (get_window()->invalidate_layout_blocked()) {
		handled = true;
		return;
	}

	point size = get_size();
	point best_size = calculate_best_size();
	if(size.x >= best_size.x && size.y >= best_size.y) {
		place(get_origin(), size);
		handled = true;
		return;
	}

	recalculate_best_size();

	if(size.y >= best_size.y) {
		// We have enough space in the Y direction, but not in the X direction.
		// Try wrapping the content.
		request_reduce_width(size.x);
		best_size = get_best_size();

		if(size.x >= best_size.x && size.y >= best_size.y) {
			// Wrapping succeeded, we still fit vertically.
			place(get_origin(), size);
			handled = true;
			return;
		} else {
			// Wrapping failed, we no longer fit.
			// Reset the sizes of child widgets.
			layout_initialize(true);
		}
	}

	/*
	Not enough space.
	Let the event flow higher up.
	This is a pre-event handler, so the event flows upwards. */
}

point grid::recalculate_best_size()
{
	point best_size = calculate_best_size();
	set_layout_size(best_size);
	return best_size;
}

point grid::calculate_best_size() const
{
	log_scope2(log_gui_layout, LOG_SCOPE_HEADER);

	// Reset the cached values.
	row_height_.clear();
	row_height_.resize(rows_, 0);
	col_width_.clear();
	col_width_.resize(cols_, 0);

	// First get the sizes for all items.
	for(unsigned row = 0; row < rows_; ++row) {
		for(unsigned col = 0; col < cols_; ++col) {

			const point size = get_child(row, col).get_best_size();

			if(size.x > static_cast<int>(col_width_[col])) {
				col_width_[col] = size.x;
			}

			if(size.y > static_cast<int>(row_height_[row])) {
				row_height_[row] = size.y;
			}
		}
	}

	for(unsigned row = 0; row < rows_; ++row) {
		DBG_GUI_L << LOG_HEADER << " the row_height_ for row " << row
				  << " will be " << row_height_[row] << ".";
	}

	for(unsigned col = 0; col < cols_; ++col) {
		DBG_GUI_L << LOG_HEADER << " the col_width_ for column " << col
				  << " will be " << col_width_[col] << ".";
	}

	const point result(
			std::accumulate(col_width_.begin(), col_width_.end(), 0),
			std::accumulate(row_height_.begin(), row_height_.end(), 0));

	DBG_GUI_L << LOG_HEADER << " returning " << result << ".";
	return result;
}

bool grid::can_wrap() const
{
	for(const auto & child : children_)
	{
		if(child.can_wrap()) {
			return true;
		}
	}

	// Inherited.
	return widget::can_wrap();
}

void grid::place(const point& origin, const point& size)
{
	log_scope2(log_gui_layout, LOG_SCOPE_HEADER);

	/***** INIT *****/

	widget::place(origin, size);

	if(!rows_ || !cols_) {
		return;
	}

	// call the calculate so the size cache gets updated.
	const point best_size = calculate_best_size();

	assert(row_height_.size() == rows_);
	assert(col_width_.size() == cols_);
	assert(row_grow_factor_.size() == rows_);
	assert(col_grow_factor_.size() == cols_);

	DBG_GUI_L << LOG_HEADER << " best size " << best_size << " available size "
			  << size << ".";

	/***** BEST_SIZE *****/

	if(best_size == size) {
		layout(origin);
		return;
	}

	if(best_size.x > size.x || best_size.y > size.y) {
		// The assertion below fails quite often so try to give as much information as possible.
		std::stringstream out;
		out << " Failed to place a grid, we have " << size << " space but we need " << best_size << " space.";
		out << " This happened at a grid with the id '" << id() << "'";
		widget* pw = parent();
		while(pw != nullptr) {
			out << " in a '" << typeid(*pw).name() << "' with the id '" << pw->id() << "'";
			pw = pw->parent();
		}
		ERR_GUI_L << LOG_HEADER << out.str() << ".";

		return;
	}

	/***** GROW *****/

	// expand it.
	if(size.x > best_size.x) {
		const unsigned w = size.x - best_size.x;
		unsigned w_size = std::accumulate(
			col_grow_factor_.begin(), col_grow_factor_.end(), 0);

		DBG_GUI_L << LOG_HEADER << " extra width " << w
			<< " will be divided amount " << w_size << " units in "
			<< cols_ << " columns.";

		if(w_size == 0) {
			// If all sizes are 0 reset them to 1
			for(auto & val : col_grow_factor_)
			{
				val = 1;
			}
			w_size = cols_;
		}
		// We might have a bit 'extra' if the division doesn't fix exactly
		// but we ignore that part for now.
		const unsigned w_normal = w / w_size;
		for(unsigned i = 0; i < cols_; ++i) {
			col_width_[i] += w_normal * col_grow_factor_[i];
			DBG_GUI_L << LOG_HEADER << " column " << i
				<< " with grow factor " << col_grow_factor_[i]
			<< " set width to " << col_width_[i] << ".";
		}
	}

	if(size.y > best_size.y) {
		const unsigned h = size.y - best_size.y;
		unsigned h_size = std::accumulate(
			row_grow_factor_.begin(), row_grow_factor_.end(), 0);
		DBG_GUI_L << LOG_HEADER << " extra height " << h
			<< " will be divided amount " << h_size << " units in "
			<< rows_ << " rows.";

		if(h_size == 0) {
			// If all sizes are 0 reset them to 1
			for(auto & val : row_grow_factor_)
			{
				val = 1;
			}
			h_size = rows_;
		}
		// We might have a bit 'extra' if the division doesn't fix exactly
		// but we ignore that part for now.
		const unsigned h_normal = h / h_size;
		for(unsigned i = 0; i < rows_; ++i) {
			row_height_[i] += h_normal * row_grow_factor_[i];
			DBG_GUI_L << LOG_HEADER << " row " << i << " with grow factor "
				<< row_grow_factor_[i] << " set height to "
				<< row_height_[i] << ".";
		}
	}

	layout(origin);
	return;
}

void grid::set_origin(const point& origin)
{
	const point movement {origin.x - get_x(), origin.y - get_y()};

	// Inherited.
	widget::set_origin(origin);

	for(auto & child : children_)
	{

		widget* widget = child.get_widget();
		assert(widget);

		widget->set_origin(point(widget->get_x() + movement.x, widget->get_y() + movement.y));
	}
}

void grid::set_visible_rectangle(const rect& rectangle)
{
	// Inherited.
	widget::set_visible_rectangle(rectangle);

	for(auto & child : children_)
	{

		widget* widget = child.get_widget();
		assert(widget);

		widget->set_visible_rectangle(rectangle);
	}
}

void grid::layout_children()
{
	for(auto & child : children_)
	{
		assert(child.get_widget());
		child.get_widget()->layout_children();
	}
}

widget* grid::find_at(const point& coordinate, const bool must_be_active)
{
	return grid_implementation::find_at<widget>(
			*this, coordinate, must_be_active);
}

const widget* grid::find_at(const point& coordinate,
							  const bool must_be_active) const
{
	return grid_implementation::find_at<const widget>(
			*this, coordinate, must_be_active);
}

widget* grid::find(const std::string_view id, const bool must_be_active)
{
	return grid_implementation::find<widget>(*this, id, must_be_active);
}

const widget* grid::find(const std::string_view id, const bool must_be_active) const
{
	return grid_implementation::find<const widget>(*this, id, must_be_active);
}

bool grid::has_widget(const widget& widget) const
{
	if(widget::has_widget(widget)) {
		return true;
	}

	for(const auto & child : children_)
	{
		if(child.get_widget()->has_widget(widget)) {
			return true;
		}
	}
	return false;
}

bool grid::disable_click_dismiss() const
{
	if(get_visible() != widget::visibility::visible) {
		return false;
	}

	for(const auto & child : children_)
	{
		const widget* widget = child.get_widget();
		assert(widget);

		if(widget->disable_click_dismiss()) {
			return true;
		}
	}
	return false;
}

iteration::walker_ptr grid::create_walker()
{
	return std::make_unique<gui2::iteration::grid>(*this);
}

void grid::set_rows(const unsigned rows)
{
	if(rows == rows_) {
		return;
	}

	set_rows_cols(rows, cols_);
}

void grid::set_cols(const unsigned cols)
{
	if(cols == cols_) {
		return;
	}

	set_rows_cols(rows_, cols);
}

void grid::set_rows_cols(const unsigned rows, const unsigned cols)
{
	if(rows == rows_ && cols == cols_) {
		return;
	}

	if(!children_.empty()) {
		WRN_GUI_G << LOG_HEADER << " resizing a non-empty grid "
				  << " may give unexpected problems.";
	}

	rows_ = rows;
	cols_ = cols;
	row_grow_factor_.resize(rows);
	col_grow_factor_.resize(cols);
	children_.resize(static_cast<std::size_t>(rows_) * cols_);
}

point grid::child::get_best_size() const
{
	log_scope2(log_gui_layout, LOG_CHILD_SCOPE_HEADER)

	if(!widget_) {
		DBG_GUI_L << LOG_CHILD_HEADER << " has widget " << false
				  << " returning " << border_space() << ".";
		return border_space();
	}

	if(widget_->get_visible() == widget::visibility::invisible) {
		DBG_GUI_L << LOG_CHILD_HEADER << " has widget " << true
				  << " widget visible " << false << " returning 0,0.";
		return point();
	}

	const point best_size = widget_->get_best_size() + border_space();

	DBG_GUI_L << LOG_CHILD_HEADER << " has widget " << true
			  << " widget visible " << true << " returning " << best_size
			  << ".";
	return best_size;
}

void grid::child::place(point origin, point size)
{
	assert(get_widget());
	if(get_widget()->get_visible() == widget::visibility::invisible) {
		return;
	}

	if(border_size_) {
		if(flags_ & BORDER_TOP) {
			origin.y += border_size_;
			size.y -= border_size_;
		}
		if(flags_ & BORDER_BOTTOM) {
			size.y -= border_size_;
		}

		if(flags_ & BORDER_LEFT) {
			origin.x += border_size_;
			size.x -= border_size_;
		}
		if(flags_ & BORDER_RIGHT) {
			size.x -= border_size_;
		}
	}

	// If size smaller or equal to best size set that size.
	// No need to check > min size since this is what we got.
	const point best_size = get_widget()->get_best_size();
	if(size <= best_size) {
		DBG_GUI_L << LOG_CHILD_HEADER
				  << " in best size range setting widget to " << origin << " x "
				  << size << ".";

		get_widget()->place(origin, size);
		return;
	}

	const styled_widget* control = dynamic_cast<const styled_widget*>(get_widget());
	const point maximum_size = control ? control->get_config_maximum_size()
										: point();

	if((flags_ & (HORIZONTAL_MASK | VERTICAL_MASK))
	   == (HORIZONTAL_GROW_SEND_TO_CLIENT | VERTICAL_GROW_SEND_TO_CLIENT)) {

		if(maximum_size == point() || size <= maximum_size) {

			DBG_GUI_L << LOG_CHILD_HEADER
					  << " in maximum size range setting widget to " << origin
					  << " x " << size << ".";

			get_widget()->place(origin, size);
			return;
		}
	}

	point widget_size = point(std::min(size.x, best_size.x), std::min(size.y, best_size.y));
	point widget_orig = origin;

	const unsigned v_flag = flags_ & VERTICAL_MASK;

	if(v_flag == VERTICAL_GROW_SEND_TO_CLIENT) {
		if(maximum_size.y) {
			widget_size.y = std::min(size.y, maximum_size.y);
		} else {
			widget_size.y = size.y;
		}
		DBG_GUI_L << LOG_CHILD_HEADER << " vertical growing from "
				  << best_size.y << " to " << widget_size.y << ".";

	} else if(v_flag == VERTICAL_ALIGN_TOP) {
		// Do nothing.

		DBG_GUI_L << LOG_CHILD_HEADER << " vertically aligned at the top.";

	} else if(v_flag == VERTICAL_ALIGN_CENTER) {

		widget_orig.y += (size.y - widget_size.y) / 2;
		DBG_GUI_L << LOG_CHILD_HEADER << " vertically centered.";

	} else if(v_flag == VERTICAL_ALIGN_BOTTOM) {

		widget_orig.y += (size.y - widget_size.y);
		DBG_GUI_L << LOG_CHILD_HEADER << " vertically aligned at the bottom.";

	} else {
		ERR_GUI_L << LOG_CHILD_HEADER << " Invalid vertical alignment '"
				  << v_flag << "' specified.";
		assert(false);
	}

	const unsigned h_flag = flags_ & HORIZONTAL_MASK;

	if(h_flag == HORIZONTAL_GROW_SEND_TO_CLIENT) {
		if(maximum_size.x) {
			widget_size.x = std::min(size.x, maximum_size.x);
		} else {
			widget_size.x = size.x;
		}
		DBG_GUI_L << LOG_CHILD_HEADER << " horizontal growing from "
				  << best_size.x << " to " << widget_size.x << ".";

	} else if(h_flag == HORIZONTAL_ALIGN_LEFT) {
		// Do nothing.
		DBG_GUI_L << LOG_CHILD_HEADER << " horizontally aligned at the left.";

	} else if(h_flag == HORIZONTAL_ALIGN_CENTER) {

		widget_orig.x += (size.x - widget_size.x) / 2;
		DBG_GUI_L << LOG_CHILD_HEADER << " horizontally centered.";

	} else if(h_flag == HORIZONTAL_ALIGN_RIGHT) {

		widget_orig.x += (size.x - widget_size.x);
		DBG_GUI_L << LOG_CHILD_HEADER
				  << " horizontally aligned at the right.";

	} else {
		ERR_GUI_L << LOG_CHILD_HEADER << " No horizontal alignment '" << h_flag
				  << "' specified.";
		assert(false);
	}

	DBG_GUI_L << LOG_CHILD_HEADER << " resize widget to " << widget_orig
			  << " x " << widget_size << ".";

	get_widget()->place(widget_orig, widget_size);
}

void grid::child::layout_initialize(const bool full_initialization)
{
	assert(widget_);

	if(widget_->get_visible() != widget::visibility::invisible) {
		widget_->layout_initialize(full_initialization);
	}
}

const std::string& grid::child::id() const
{
	assert(widget_);
	return widget_->id();
}

point grid::child::border_space() const
{
	point result(0, 0);

	if(border_size_) {

		if(flags_ & BORDER_TOP)
			result.y += border_size_;
		if(flags_ & BORDER_BOTTOM)
			result.y += border_size_;

		if(flags_ & BORDER_LEFT)
			result.x += border_size_;
		if(flags_ & BORDER_RIGHT)
			result.x += border_size_;
	}

	return result;
}

grid::child* grid::get_child(widget* w)
{
	if(!w) {
		return nullptr;
	}

	for(auto& child : children_) {
		if(w == child.get_widget()) {
			return &child;
		}
	}

	return nullptr;
}

void grid::set_child_alignment(widget* widget, unsigned set_flag, unsigned mode_mask)
{
	grid::child* cell = get_child(widget);
	if(!cell) {
		return;
	}

	unsigned flags = cell->get_flags();

	if((flags & mode_mask) == HORIZONTAL_GROW_SEND_TO_CLIENT) {
		ERR_GUI_G << "Cannot set horizontal alignment (grid cell specifies dynamic growth)";
		return;
	}

	if((flags & mode_mask) == VERTICAL_GROW_SEND_TO_CLIENT) {
		ERR_GUI_G << "Cannot set vertical alignment (grid cell specifies dynamic growth)";
		return;
	}

	flags &= ~mode_mask;
	flags |= set_flag;

	cell->set_flags(flags);

	event::message message;
	fire(event::REQUEST_PLACEMENT, *this, message);
}

void grid::layout(const point& origin)
{
	point orig = origin;
	for(unsigned row = 0; row < rows_; ++row) {
		for(unsigned col = 0; col < cols_; ++col) {

			const point size(col_width_[col], row_height_[row]);
			DBG_GUI_L << LOG_HEADER << " set widget at " << row << ',' << col
					  << " at origin " << orig << " with size " << size
					  << ".";

			if(get_child(row, col).get_widget()) {
				get_child(row, col).place(orig, size);
			}

			orig.x += col_width_[col];
		}
		orig.y += row_height_[row];
		orig.x = origin.x;
	}
}

void grid::impl_draw_children()
{
	// TODO: draw_manager - what is this comment talking about here? something that was removed?
	/*
	 * The call to SDL_PumpEvents seems a bit like black magic.
	 * With the call the resizing doesn't seem to lose resize events.
	 * But when added the queue still only contains one resize event and the
	 * internal SDL queue doesn't seem to overflow (rarely more than 50 pending
	 * events).
	 * Without the call when resizing larger a black area of remains, this is
	 * the area not used for resizing the screen, this call `fixes' that.
	 */

	assert(get_visible() == widget::visibility::visible);

	// TODO: draw_manager - don't draw children outside clip area. This is problematic because either clip area is not correct here or widget positions are not absolute
	for(auto & child : children_)
	{
		widget* widget = child.get_widget();
		assert(widget);

		if(widget->get_visible() != widget::visibility::visible) {
			continue;
		}

		if(widget->get_drawing_action() == widget::redraw_action::none) {
			continue;
		}

		// We may need to defer drawing to next frame for blur processing.
		if(!widget->draw_background()) {
			get_window()->defer_region(widget->get_rectangle());
			continue;
		}

		widget->draw_children();

		if(!widget->draw_foreground()) {
			get_window()->defer_region(widget->get_rectangle());
			continue;
		}
	}
}

unsigned grid_implementation::row_request_reduce_height(
		grid& grid, const unsigned row, const unsigned maximum_height)
{
	// The minimum height required.
	unsigned required_height = 0;

	for(std::size_t x = 0; x < grid.cols_; ++x) {
		grid::child& cell = grid.get_child(row, x);
		cell_request_reduce_height(cell, maximum_height);

		const point size(cell.get_best_size());

		if(required_height == 0 || static_cast<std::size_t>(size.y)
								   > required_height) {

			required_height = size.y;
		}
	}

	DBG_GUI_L << LOG_IMPL_HEADER << " maximum row height " << maximum_height
			  << " returning " << required_height << ".";

	return required_height;
}

unsigned grid_implementation::column_request_reduce_width(
		grid& grid, const unsigned column, const unsigned maximum_width)
{
	// The minimum width required.
	unsigned required_width = 0;

	for(std::size_t y = 0; y < grid.rows_; ++y) {
		grid::child& cell = grid.get_child(y, column);
		cell_request_reduce_width(cell, maximum_width);

		const point size(cell.get_best_size());

		if(required_width == 0 || static_cast<std::size_t>(size.x)
								  > required_width) {

			required_width = size.x;
		}
	}

	DBG_GUI_L << LOG_IMPL_HEADER << " maximum column width " << maximum_width
			  << " returning " << required_width << ".";

	return required_width;
}

void
grid_implementation::cell_request_reduce_height(grid::child& child,
												 const unsigned maximum_height)
{
	assert(child.widget_);

	if(child.widget_->get_visible() == widget::visibility::invisible) {
		return;
	}

	child.widget_->request_reduce_height(maximum_height
										 - child.border_space().y);
}

void
grid_implementation::cell_request_reduce_width(grid::child& child,
												const unsigned maximum_width)
{
	assert(child.widget_);

	if(child.widget_->get_visible() == widget::visibility::invisible) {
		return;
	}

	child.widget_->request_reduce_width(maximum_width - child.border_space().x);
}

void set_single_child(grid& grid, std::unique_ptr<widget> widget)
{
	grid.set_rows_cols(1, 1);
	grid.set_child(std::move(widget),
				   0,
				   0,
				   grid::HORIZONTAL_GROW_SEND_TO_CLIENT
				   | grid::VERTICAL_GROW_SEND_TO_CLIENT,
				   0);
}

} // namespace gui2
