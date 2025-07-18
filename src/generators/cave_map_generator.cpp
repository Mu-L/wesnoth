/*
	Copyright (C) 2003 - 2025
	by David White <dave@whitevine.net>
	Part of the Battle for Wesnoth Project https://www.wesnoth.org/

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.
	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY.

	See the COPYING file for more details.
*/

/**
 * @file
 * Map-generator for caves.
 */

#include "generators/cave_map_generator.hpp"
#include "log.hpp"
#include "map/map.hpp"
#include "pathfind/pathfind.hpp"
#include "serialization/string_utils.hpp"
#include "seed_rng.hpp"

static lg::log_domain log_engine("engine");
#define LOG_NG LOG_STREAM(info, log_engine)

static lg::log_domain log_wml("wml");
#define ERR_WML LOG_STREAM(err, log_wml)

cave_map_generator::cave_map_generator(const config &cfg) :
	wall_(t_translation::CAVE_WALL),
	clear_(t_translation::CAVE),
	village_(t_translation::UNDERGROUND_VILLAGE),
	castle_(t_translation::DWARVEN_CASTLE),
	keep_(t_translation::DWARVEN_KEEP),
	cfg_(cfg),
	width_(cfg_["map_width"].to_int(50)),
	height_(cfg_["map_height"].to_int(50)),
	village_density_(cfg_["village_density"].to_int(0)),
	flipx_chance_(cfg_["flipx_chance"].to_int()),
	flipy_chance_(cfg_["flipy_chance"].to_int())
{
}

std::string cave_map_generator::config_name() const
{
	return "";
}

std::size_t cave_map_generator::cave_map_generator_job::translate_x(std::size_t x) const
{
	if(flipx_) {
		x = params.width_ - x - 1;
	}

	return x;
}

std::size_t cave_map_generator::cave_map_generator_job::translate_y(std::size_t y) const
{
	if(flipy_) {
		y = params.height_ - y - 1;
	}

	return y;
}

std::string cave_map_generator::create_map(utils::optional<uint32_t> randomseed)
{
	const config res = create_scenario(randomseed);
	return res["map_data"];
}

config cave_map_generator::create_scenario(utils::optional<uint32_t> randomseed)
{
	cave_map_generator_job job(*this, randomseed);
	return job.res_;
}

cave_map_generator::cave_map_generator_job::cave_map_generator_job(const cave_map_generator& pparams, utils::optional<uint32_t> randomseed)
	: params(pparams)
	, flipx_(false)
	, flipy_(false)
	, map_(t_translation::ter_map(params.width_ + 2 * gamemap::default_border, params.height_ + 2 * gamemap::default_border/*, params.wall_*/))
	, starting_positions_()
	, chamber_ids_()
	, chambers_()
	, passages_()
	, res_(params.cfg_.child_or_empty("settings"))
	, rng_() //initialises with rand()
{
	res_.add_child("event", config {
		"name", "start",
		"deprecated_message", config {
			"what", "scenario_generation=cave",
			"level", 1,
			"message", "Use the Lua cave generator instead, with scenario_generation=lua and create_scenario= (see wiki for details).",
		},
	});
	uint32_t seed = randomseed ? *randomseed : seed_rng::next_seed();
	rng_.seed(seed);
	LOG_NG << "creating random cave with seed: " << seed;
	flipx_ = static_cast<int>(rng_() % 100) < params.flipx_chance_;
	flipy_ = static_cast<int>(rng_() % 100) < params.flipy_chance_;

	LOG_NG << "creating scenario....";
	generate_chambers();

	LOG_NG << "placing chambers...";
	for(const chamber& c : chambers_) {
		place_chamber(c);
	}

	LOG_NG << "placing passages...";

	for(const passage& p : passages_) {
		place_passage(p);
	}
	LOG_NG << "outputting map....";

	res_["map_data"] = t_translation::write_game_map(map_, starting_positions_);
}

void cave_map_generator::cave_map_generator_job::build_chamber(map_location loc, std::set<map_location>& locs, std::size_t size, std::size_t jagged)
{
	if(size == 0 || locs.count(loc) != 0 || !params.on_board(loc))
		return;

	locs.insert(loc);

	for(const map_location& adj : get_adjacent_tiles(loc)) {
		if(static_cast<int>(rng_() % 100) < (100l - static_cast<long>(jagged))) {
			build_chamber(adj,locs,size-1,jagged);
		}
	}
}

void cave_map_generator::cave_map_generator_job::generate_chambers()
{
	for (const config &ch : params.cfg_.child_range("chamber"))
	{
		// If there is only a chance of the chamber appearing, deal with that here.
		if (ch.has_attribute("chance") && static_cast<int>(rng_() % 100) < ch["chance"].to_int()) {
			continue;
		}

		const std::string &xpos = ch["x"];
		const std::string &ypos = ch["y"];

		std::size_t min_xpos = 0, min_ypos = 0, max_xpos = params.width_, max_ypos = params.height_;

		if (!xpos.empty()) {
			const std::vector<std::string>& items = utils::split(xpos, '-');
			if(items.empty() == false) {
				try {
					min_xpos = std::stoi(items.front()) - 1;
					max_xpos = std::stoi(items.back());
				} catch(const std::invalid_argument&) {
					lg::log_to_chat() << "Invalid min/max coordinates in cave_map_generator: " << items.front() << ", " << items.back() << "\n";
					ERR_WML << "Invalid min/max coordinates in cave_map_generator: " << items.front() << ", " << items.back();
					continue;
				}
			}
		}

		if (!ypos.empty()) {
			const std::vector<std::string>& items = utils::split(ypos, '-');
			if(items.empty() == false) {
				try {
					min_ypos = std::stoi(items.front()) - 1;
					max_ypos = std::stoi(items.back());
				} catch(const std::invalid_argument&) {
					lg::log_to_chat() << "Invalid min/max coordinates in cave_map_generator: " << items.front() << ", " << items.back() << "\n";
					ERR_WML << "Invalid min/max coordinates in cave_map_generator: " << items.front() << ", " << items.back();
				}
			}
		}
		const std::size_t x = translate_x(min_xpos + (rng_()%(max_xpos-min_xpos)));
		const std::size_t y = translate_y(min_ypos + (rng_()%(max_ypos-min_ypos)));

		int chamber_size = ch["size"].to_int(3);
		int jagged_edges = ch["jagged"].to_int();

		chamber new_chamber;
		new_chamber.center = map_location(x,y);
		build_chamber(new_chamber.center,new_chamber.locs,chamber_size,jagged_edges);

		auto items = ch.optional_child("items");
		new_chamber.items = items ? &*items : nullptr;

		const std::string &id = ch["id"];
		if (!id.empty()) {
			chamber_ids_[id] = chambers_.size();
		}

		chambers_.push_back(new_chamber);

		for(const config &p : ch.child_range("passage"))
		{
			const std::string &dst = p["destination"];

			// Find the destination of this passage
			const std::map<std::string,std::size_t>::const_iterator itor = chamber_ids_.find(dst);
			if(itor == chamber_ids_.end())
				continue;

			assert(itor->second < chambers_.size());

			passages_.emplace_back(new_chamber.center, chambers_[itor->second].center, p);
		}
	}
}

void cave_map_generator::cave_map_generator_job::place_chamber(const chamber& c)
{
	for(const map_location& loc : c.locs) {
		set_terrain(loc, params.clear_);
	}

	if (c.items == nullptr || c.locs.empty()) return;

	std::size_t index = 0;
	for(const auto [child_key, child_cfg] : c.items->all_children_view())
	{
		config cfg = child_cfg;
		auto filter = cfg.optional_child("filter");
		config* object_filter = nullptr;
		if (auto object = cfg.optional_child("object")) {
			if (auto of = object->optional_child("filter")) {
				object_filter = &*of;
			}
		}

		if (!child_cfg["same_location_as_previous"].to_bool()) {
			index = rng_()%c.locs.size();
		}
		std::string loc_var = child_cfg["store_location_as"];

		std::set<map_location>::const_iterator loc = c.locs.begin();
		std::advance(loc,index);

		cfg["x"] = loc->x + 1;
		cfg["y"] = loc->y + 1;

		if (filter) {
			filter["x"] = loc->x + 1;
			filter["y"] = loc->y + 1;
		}

		if (object_filter) {
			(*object_filter)["x"] = loc->x + 1;
			(*object_filter)["y"] = loc->y + 1;
		}

		// If this is a side, place a castle for the side
		if (child_key == "side" && !child_cfg["no_castle"].to_bool()) {
			place_castle(child_cfg["side"].to_int(-1), *loc);
		}

		res_.add_child(child_key, cfg);

		if(!loc_var.empty()) {
			config &temp = res_.add_child("event");
			temp["name"] = "prestart";
			config &xcfg = temp.add_child("set_variable");
			xcfg["name"] = loc_var + "_x";
			xcfg["value"] = loc->x + 1;
			config &ycfg = temp.add_child("set_variable");
			ycfg["name"] = loc_var + "_y";
			ycfg["value"] = loc->y + 1;
		}
	}
}

struct passage_path_calculator : pathfind::cost_calculator
{
	passage_path_calculator(const t_translation::ter_map& mapdata,
	                        const t_translation::terrain_code & wall,
	                        double laziness, std::size_t windiness,
							std::mt19937& rng) :
		map_(mapdata), wall_(wall), laziness_(laziness), windiness_(windiness), rng_(rng)
	{}

	virtual double cost(const map_location& loc, const double so_far) const;
private:
	const t_translation::ter_map& map_;
	t_translation::terrain_code wall_;
	double laziness_;
	std::size_t windiness_;
	std::mt19937& rng_;
};

double passage_path_calculator::cost(const map_location& loc, const double) const
{
	double res = 1.0;
	if (map_.get(loc.x + gamemap::default_border, loc.y + gamemap::default_border) == wall_) {
		res = laziness_;
	}

	if(windiness_ > 1) {
		res *= static_cast<double>(rng_()%windiness_);
	}

	return res;
}

void cave_map_generator::cave_map_generator_job::place_passage(const passage& p)
{
	const std::string& chance = p.cfg["chance"];
	if(!chance.empty() && static_cast<int>(rng_()%100) < std::stoi(chance)) {
		return;
	}

	int windiness = p.cfg["windiness"].to_int();
	double laziness = std::max<double>(1.0, p.cfg["laziness"].to_double());

	passage_path_calculator calc(map_, params.wall_, laziness, windiness, rng_);

	pathfind::plain_route rt = a_star_search(p.src, p.dst, 10000.0, calc, params.width_, params.height_);

	int width = std::max<int>(1, p.cfg["width"].to_int());
	int jagged = p.cfg["jagged"].to_int();

	for(const map_location& step : rt.steps) {
		std::set<map_location> locs;
		build_chamber(step, locs, width, jagged);
		for(const map_location& loc : locs) {
			set_terrain(loc, params.clear_);
		}
	}
}

void cave_map_generator::cave_map_generator_job::set_terrain(map_location loc, const t_translation::terrain_code & t)
{
	if (params.on_board(loc)) {
		t_translation::terrain_code& c = map_.get(loc.x + gamemap::default_border, loc.y + gamemap::default_border);

		if(c == params.clear_ || c == params.wall_ || c == params.village_) {
			// Change this terrain.
			if ( t == params.clear_  &&  static_cast<int>(rng_() % 1000) < params.village_density_ )
				// Override with a village.
				c = params.village_;
			else
				c = t;
		}
	}
}

void cave_map_generator::cave_map_generator_job::place_castle(int starting_position, const map_location &loc)
{
	if (starting_position != -1) {
		set_terrain(loc, params.keep_);

		t_translation::coordinate coord(
				  loc.x + gamemap::default_border
				, loc.y + gamemap::default_border);
		starting_positions_.insert(t_translation::starting_positions::value_type(std::to_string(starting_position), coord));
	}

	for(const map_location& adj : get_adjacent_tiles(loc)) {
		set_terrain(adj, params.castle_);
	}
}
