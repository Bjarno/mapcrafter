/*
 * Copyright 2012-2015 Moritz Hilscher
 *
 * This file is part of Mapcrafter.
 *
 * Mapcrafter is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Mapcrafter is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Mapcrafter.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "overlay.h"

#include "heightoverlay.h"
#include "lighting.h"
#include "slimeoverlay.h"
#include "spawnoverlay.h"
#include "../blockimages.h"
#include "../image.h"
#include "../../config/mapcrafterconfig.h"
#include "../../config/configsections/map.h"
#include "../../config/configsections/overlay.h"
#include "../../config/configsections/world.h"
#include "../../mc/pos.h"

namespace mapcrafter {
namespace renderer {

OverlayRenderer::OverlayRenderer()
	: high_contrast(true) {
}

OverlayRenderer::~OverlayRenderer() {
}

void OverlayRenderer::setHighContrast(bool high_contrast) {
	this->high_contrast = high_contrast;
}

void OverlayRenderer::tintBlock(RGBAImage& image, RGBAPixel color) const {
	/*
	if (high_contrast) {
		// do the high contrast mode magic
		auto overlay = getRecolor(color);
		for (int y = 0; y < image.getWidth(); y++) {
			for (int x = 0; x < image.getHeight(); x++) {
				RGBAPixel& pixel = image.pixel(x, y);
				if (pixel != 0)
					pixel = rgba_add_clamp(pixel, overlay);
			}
		}
	} else {
		// otherwise just simple alphablending
		for (int y = 0; y < image.getWidth(); y++) {
			for (int x = 0; x < image.getHeight(); x++) {
				RGBAPixel& pixel = image.pixel(x, y);
				if (pixel != 0)
					blend(pixel, color);
			}
		}
	}
	*/

	image.fill(color);
}

std::tuple<int, int, int> OverlayRenderer::getRecolor(RGBAPixel color) const {
	// get luminance of recolor:
	// "10*r + 3*g + b" should actually be "3*r + 10*g + b"
	// it was a typo, but doesn't look bad either
	int luminance = (10 * rgba_red(color) + 3 * rgba_green(color) + rgba_blue(color)) / 14;

	float alpha_factor = 3; // 3 is similar to alpha=85
	// something like that would be possible too, but overlays won't look exactly like
	// overlays with that alpha value, so don't use it for now
	// alpha_factor = (float) 255.0 / rgba_alpha(color);

	// try to do luminance-neutral additive/subtractive color
	// instead of alpha blending (for better contrast)
	// so first subtract luminance from each component
	int nr = (rgba_red(color) - luminance) / alpha_factor;
	int ng = (rgba_green(color) - luminance) / alpha_factor;
	int nb = (rgba_blue(color) - luminance) / alpha_factor;
	return std::make_tuple(nr, ng, nb);
}

const RenderModeRendererType OverlayRenderer::TYPE = RenderModeRendererType::OVERLAY;

TintingOverlay::TintingOverlay(OverlayMode overlay_mode, const std::string& id,
		const std::string& name)
	: BaseOverlayRenderMode(id, name, false), overlay_mode(overlay_mode) {
}

TintingOverlay::~TintingOverlay() {
}

void TintingOverlay::drawOverlay(RGBAImage& block, RGBAImage& overlay,
		const mc::BlockPos& pos, uint16_t id, uint16_t data) {
	if (overlay_mode == OverlayMode::PER_BLOCK) {
		// simple mode where we just tint whole blocks
		RGBAPixel color = getBlockColor(pos, id, data);
		if (rgba_alpha(color) == 0)
			return;
		renderer->tintBlock(overlay, color);
	} else {
		// "advanced" mode where each block/position has a color,
		// and adjacent faces are tinted / or the transparent blocks themselves
		// TODO potential for optimization, maybe cache colors of blocks?
		if (images->isBlockTransparent(id, data)) {
			RGBAPixel color = getBlockColor(pos, id, data);
			if (rgba_alpha(color) == 0)
				return;
			renderer->tintBlock(overlay, color);
		} else {
			mc::Block top, left, right;
			RGBAPixel color_top, color_left, color_right;
			top = getBlock(pos + mc::DIR_TOP, mc::GET_ID | mc::GET_DATA);
			left = getBlock(pos + mc::DIR_WEST, mc::GET_ID | mc::GET_DATA);
			right = getBlock(pos + mc::DIR_SOUTH, mc::GET_ID | mc::GET_DATA);
			color_top = getBlockColor(pos + mc::DIR_TOP, top.id, top.data);
			color_left = getBlockColor(pos + mc::DIR_WEST, left.id, left.data);
			color_right = getBlockColor(pos + mc::DIR_SOUTH, right.id, right.data);
			
			if (rgba_alpha(color_top) != 0)
				renderer->tintTop(overlay, color_top, 0);
			if (rgba_alpha(color_left) != 0)
				renderer->tintLeft(overlay, color_left);
			if (rgba_alpha(color_right) != 0)
				renderer->tintRight(overlay, color_right);
		}
	}
}

OverlayRenderMode* createOverlay(const config::WorldSection& world_config,
		const config::MapSection& map_config,
		const config::OverlaySection& overlay_config, int rotation) {
	OverlayType type = overlay_config.getType();
	std::string id = overlay_config.getID();
	std::string name = overlay_config.getName();

	if (type == OverlayType::LIGHTING) {
		return new LightingRenderMode(id, name, true, 1.0, 1.0, false);
	} else if (type == OverlayType::SLIME) {
		return new SlimeOverlay(world_config.getInputDir(), rotation);
	} else if (type == OverlayType::SPAWN) {
		return new SpawnOverlay(true);
	} else {
		// may not happen
		assert(false);
	}
	return nullptr;
}

std::vector<std::shared_ptr<OverlayRenderMode>> createOverlays(
		const config::WorldSection& world_config, const config::MapSection& map_config,
		const std::map<std::string, config::OverlaySection>& overlays_config,
		int rotation) {
	std::vector<std::shared_ptr<OverlayRenderMode>> overlays;
	
	auto overlay_types = map_config.getOverlays();
	for (auto it = overlay_types.begin(); it != overlay_types.end(); ++it) {
		config::OverlaySection overlay_config = overlays_config.at(*it);
		OverlayRenderMode* overlay = createOverlay(world_config, map_config,
				overlay_config, rotation);
		overlays.push_back(std::shared_ptr<OverlayRenderMode>(overlay));
		
		/*
		if (overlay_type == OverlayType::SLIME)
			overlay = new SlimeOverlay(world_config.getInputDir(), rotation);
		else if (overlay_type == OverlayType::SPAWNDAY)
			overlay = new SpawnOverlay(true);
		else if (overlay_type == OverlayType::SPAWNNIGHT)
			overlay = new SpawnOverlay(false);
		else if (overlay_type == OverlayType::DAY)
			overlay = new LightingRenderMode("day", "Day", true,
					map_config.getLightingIntensity(), map_config.getLightingWaterIntensity(),
					world_config.getDimension() == mc::Dimension::END);
		else if (overlay_type == OverlayType::NIGHT)
			overlay = new LightingRenderMode("night", "Night", false,
					map_config.getLightingIntensity(), map_config.getLightingWaterIntensity(),
					world_config.getDimension() == mc::Dimension::END);
		else {
			// should not happen
			assert(false);
		}
		*/
	}

	return overlays;
}

}
}

