
/*******************************************************************
 * SLADE - It's a Doom Editor
 * Copyright (C) 2008-2014 Simon Judd
 *
 * Email:       sirjuddington@gmail.com
 * Web:         http://slade.mancubus.net
 * Filename:    MapRenderer3D.cpp
 * Description: MapRenderer3D class - handles all rendering related
 *              stuff for 3d mode
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 *******************************************************************/


/*******************************************************************
 * INCLUDES
 *******************************************************************/
#include "Main.h"
#include "UI/WxStuff.h"
#include "MapRenderer3D.h"
#include "MapEditor/SLADEMap/SLADEMap.h"
#include "Utility/MathStuff.h"
#include "MapEditor/MapEditorWindow.h"
#include "General/ColourConfiguration.h"
#include "General/ResourceManager.h"
#include "MainEditor/MainWindow.h"
#include "OpenGL/OpenGL.h"
#include <SFML/System.hpp>


/*******************************************************************
 * VARIABLES
 *******************************************************************/
CVAR(Float, render_max_dist, 2000, CVAR_SAVE)
CVAR(Float, render_max_thing_dist, 2000, CVAR_SAVE)
CVAR(Int, render_thing_icon_size, 16, CVAR_SAVE)
CVAR(Bool, render_fog_quality, true, CVAR_SAVE)
CVAR(Bool, render_max_dist_adaptive, false, CVAR_SAVE)
CVAR(Int, render_adaptive_ms, 15, CVAR_SAVE)
CVAR(Bool, render_3d_sky, true, CVAR_SAVE)
CVAR(Int, render_3d_things, 1, CVAR_SAVE)
CVAR(Int, render_3d_things_style, 1, CVAR_SAVE)
CVAR(Int, render_3d_hilight, 1, CVAR_SAVE)
CVAR(Float, render_3d_brightness, 1, CVAR_SAVE)
CVAR(Float, render_fog_distance, 1500, CVAR_SAVE)
CVAR(Bool, render_fog_new_formula, true, CVAR_SAVE)
CVAR(Bool, render_shade_orthogonal_lines, true, CVAR_SAVE)


/*******************************************************************
 * EXTERNAL VARIABLES
 *******************************************************************/
EXTERN_CVAR(Bool, flats_use_vbo)
EXTERN_CVAR(Bool, use_zeth_icons)


/*******************************************************************
 * MAPRENDERER3D CLASS FUNCTIONS
 *******************************************************************/

/* MapRenderer3D::MapRenderer3D
 * MapRenderer3D class constructor
 *******************************************************************/
MapRenderer3D::MapRenderer3D(SLADEMap* map)
{
	// Init variables
	this->map = map;
	this->fog = true;
	this->fullbright = false;
	this->gravity = 0.5;
	this->vbo_flats = 0;
	this->vbo_walls = 0;
	this->skytex1 = "SKY1";
	this->quads = NULL;
	this->tex_last = NULL;
	this->n_quads = 0;
	this->n_flats = 0;
	this->flat_last = 0;
	this->render_hilight = true;
	this->render_selection = true;

	// Build skybox circle
	buildSkyCircle();

	// Init other
	init();

	// Listen to stuff
	listenTo(thePaletteChooser);
	listenTo(theResourceManager);
}

/* MapRenderer3D::~MapRenderer3D
 * MapRenderer3D class destructor
 *******************************************************************/
MapRenderer3D::~MapRenderer3D()
{
	if (quads)				delete quads;
	if (vbo_flats > 0)		glDeleteBuffers(1, &vbo_flats);
	if (vbo_walls > 0)		glDeleteBuffers(1, &vbo_walls);
}

/* MapRenderer3D::init
 * Initialises the 3d renderer
 *******************************************************************/
bool MapRenderer3D::init()
{
	// Check to enable zdoom udmf extensions
	if (S_CMPNOCASE(theGameConfiguration->udmfNamespace(), "zdoom") && map->currentFormat() == MAP_UDMF)
		udmf_zdoom = true;

	// Init camera
	bbox_t bbox = map->getMapBBox();
	cam_position.set(bbox.min.x + ((bbox.max.x - bbox.min.x)*0.5), bbox.min.y + ((bbox.max.y - bbox.min.y)*0.5), 64);
	cam_direction.set(0,1);
	cam_pitch = 0;
	cameraUpdateVectors();

	refresh();

	return true;
}

/* MapRenderer3D::refresh
 * Clears VBOs and cached data
 *******************************************************************/
void MapRenderer3D::refresh()
{
	// Clear any existing map data
	dist_sectors.clear();
	if (quads)
	{
		delete quads;
		quads = NULL;
	}
	flats.clear();

	// Clear VBOs
	if (vbo_flats != 0)
	{
		glDeleteBuffers(1, &vbo_flats);
		vbo_flats = 0;
	}

	sector_flats.clear();

	// Set sky texture
	gc_mapinfo_t minf = theGameConfiguration->mapInfo(map->mapName());
	skytex1 = minf.sky1;
	skytex2 = minf.sky2;
	skycol_top.a = 0;
	//wxLogMessage("sky1: %s, sky2: %s", skytex1, skytex2);
}

/* MapRenderer3D::clearData
 * Clears all cached rendering data
 *******************************************************************/
void MapRenderer3D::clearData()
{
	// Clear map structures
	lines.clear();
	things.clear();
	sector_flats.clear();

	// Clear everything else
	refresh();
}

/* MapRenderer3D::buildSkyCircle
 * Generates the array of points (circular) used for the sky
 *******************************************************************/
void MapRenderer3D::buildSkyCircle()
{
	double rot = 0;
	for (unsigned a = 0; a < 32; a++)
	{
		sky_circle[a].set(sin(rot), -cos(rot));
		rot -= (3.1415926535897932384626433832795 * 2) / 32.0;
	}
}

/* MapRenderer3D::getQuad
 * Returns the wall quad for wall selection [item]
 *******************************************************************/
MapRenderer3D::quad_3d_t* MapRenderer3D::getQuad(selection_3d_t item)
{
	// Check item type
	if (item.type != MapEditor::SEL_SIDE_BOTTOM &&
	        item.type != MapEditor::SEL_SIDE_MIDDLE &&
	        item.type != MapEditor::SEL_SIDE_TOP)
		return NULL;

	// Get side
	MapSide* side = map->getSide(item.index);
	if (!side)
		return NULL;

	// Find matching quad
	int lindex = side->getParentLine()->getIndex();
	for (unsigned a = 0; a < lines[lindex].quads.size(); a++)
	{
		quad_3d_t* quad = &lines[lindex].quads[a];

		// Check side
		if (side == side->getParentLine()->s1() && quad->flags & BACK)
			continue;
		if (side == side->getParentLine()->s2() && (quad->flags & BACK) == 0)
			continue;

		// Check part
		if (item.type == MapEditor::SEL_SIDE_BOTTOM)
		{
			if (quad->flags & LOWER)
				return quad;
		}
		if (item.type == MapEditor::SEL_SIDE_TOP)
		{
			if (quad->flags & UPPER)
				return quad;
		}
		if (item.type == MapEditor::SEL_SIDE_MIDDLE)
		{
			if ((quad->flags & UPPER) == 0 && (quad->flags & LOWER) == 0)
				return quad;
		}
	}

	// Not found
	return NULL;
}

/* MapRenderer3D::getFlat
 * Returns the flat for sector flat selection [item]
 *******************************************************************/
MapRenderer3D::flat_3d_t* MapRenderer3D::getFlat(selection_3d_t item)
{
	// Check index
	if ((unsigned)item.index >= sector_flats.size())
		return NULL;

	// TODO 3dfloors
	// Floor
	if (item.type == MapEditor::SEL_FLOOR)
		return &sector_flats[item.index][0];

	// Ceiling
	else if (item.type == MapEditor::SEL_CEILING)
		return &sector_flats[item.index][1];

	// Wrong type
	else
		return NULL;
}

/* MapRenderer3D::cameraMove
 * Moves the camera the direction it is facing by [distance]. If [z]
 * is false it will only be moved along x/y axes
 *******************************************************************/
void MapRenderer3D::cameraMove(double distance, bool z)
{
	// Move along direction vector
	if (z)
	{
		cam_position.x += cam_dir3d.x * distance;
		cam_position.y += cam_dir3d.y * distance;
		cam_position.z += cam_dir3d.z * distance;
	}
	else
	{
		cam_position.x += cam_direction.x * distance;
		cam_position.y += cam_direction.y * distance;
	}
}

/* MapRenderer3D::cameraTurn
 * Rotates the camera by [angle] around the z axis
 *******************************************************************/
void MapRenderer3D::cameraTurn(double angle)
{
	// Find rotated view point
	fpoint2_t cp2d(cam_position.x, cam_position.y);
	fpoint2_t nd = MathStuff::rotatePoint(cp2d, cp2d + cam_direction, angle);

	// Update direction
	cam_direction.x = nd.x - cam_position.x;
	cam_direction.y = nd.y - cam_position.y;

	// Update vectors
	cameraUpdateVectors();
}

/* MapRenderer3D::cameraMoveUp
 * Moves the camera along the z axis by [distance]
 *******************************************************************/
void MapRenderer3D::cameraMoveUp(double distance)
{
	cam_position.z += distance;
}

/* MapRenderer3D::cameraStrafe
 * Moves the camera along the strafe axis by [distance]
 *******************************************************************/
void MapRenderer3D::cameraStrafe(double distance)
{
	// Move along strafe vector
	cam_position.x += cam_strafe.x * distance;
	cam_position.y += cam_strafe.y * distance;
}

/* MapRenderer3D::cameraPitch
 * Rotates the camera view around the strafe axis by [amount]
 *******************************************************************/
void MapRenderer3D::cameraPitch(double amount)
{
	// Pitch camera
	cam_pitch += amount;

	// Clamp
	double rad90 = PI*0.5;
	if (cam_pitch > rad90)
		cam_pitch = rad90;
	if (cam_pitch < -rad90)
		cam_pitch = -rad90;

	// Update vectors
	cameraUpdateVectors();
}

/* MapRenderer3D::cameraUpdateVectors
 * Updates the strafe and direction vectors for the camera
 *******************************************************************/
void MapRenderer3D::cameraUpdateVectors()
{
	// Normalize direction
	cam_direction.normalize();

	// Calculate strafe vector
	cam_strafe = fpoint3_t(cam_direction, 0).cross(fpoint3_t(0, 0, 1));
	cam_strafe.normalize();

	// Calculate 3d direction vector
	cam_dir3d = MathStuff::rotateVector3D(fpoint3_t(cam_direction, 0), cam_strafe, cam_pitch);
	cam_dir3d.normalize();
}

/* MapRenderer3D::cameraSet
 * Sets the camera position to [position], facing [direction]
 *******************************************************************/
void MapRenderer3D::cameraSet(fpoint3_t position, fpoint2_t direction)
{
	// Set camera position/direction
	cam_position = position;
	cam_direction = direction;
	cam_pitch = 0;

	// Update camera vectors
	cameraUpdateVectors();
}

/* MapRenderer3D::cameraSetPosition
 * Moves the camera to [position]
 *******************************************************************/
void MapRenderer3D::cameraSetPosition(fpoint3_t position)
{
	cam_position = position;
}

/* MapRenderer3D::cameraApplyGravity
 * Applies gravity to the camera
 *******************************************************************/
void MapRenderer3D::cameraApplyGravity(double mult)
{
	// Get current sector
	int sector = map->sectorAt(cam_position.get2d());
	if (sector < 0)
		return;

	// Get target height
	int view_height = (map->currentFormat() == MAP_DOOM64) ? 56 : 41;
	int fheight = map->getSector(sector)->getFloorPlane().height_at(cam_position.get2d()) + view_height;
	int cheight = map->getSector(sector)->getCeilingPlane().height_at(cam_position.get2d());
	if (fheight > cheight - 4)
		fheight = cheight - 4;

	if (cam_position.z > fheight)
	{
		double diff = cam_position.z - fheight;
		cam_position.z -= (diff*0.3*mult);
		if (cam_position.z < fheight)
			cam_position.z = fheight;
	}

	else if (cam_position.z < fheight)
	{
		double diff = fheight - cam_position.z;
		cam_position.z += (diff*0.5*mult);
		if (cam_position.z > fheight)
			cam_position.z = fheight;
	}
}

/* MapRenderer3D::setupView
 * Sets up the OpenGL view/projection for rendering
 *******************************************************************/
void MapRenderer3D::setupView(int width, int height)
{
	// Calculate aspect ratio
	float aspect = (1.6f / 1.333333f) * ((float)width / (float)height);
	float fovy = 2 * MathStuff::radToDeg(atan(tan(MathStuff::degToRad(90) / 2) / aspect));

	// Setup projection
	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();

	float max = render_max_dist * 1.5f;
	if (max < 100) max = 20000;
	gluPerspective(fovy, aspect, 0.5f, max);

	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();

	// Calculate up vector
	fpoint3_t up = cam_strafe.cross(cam_dir3d).normalized();

	// Setup camera view
	gluLookAt(cam_position.x, cam_position.y, cam_position.z,
	          cam_position.x+cam_dir3d.x, cam_position.y+cam_dir3d.y, cam_position.z+cam_dir3d.z,
	          up.x, up.y, up.z);
}

/* MapRenderer3D::setLight
 * Sets the OpenGL colour for rendering an object using [colour]
 * and [light] level
 *******************************************************************/
void MapRenderer3D::setLight(rgba_t& colour, uint8_t light, float alpha)
{
	// Force 255 light in fullbright mode
	if (fullbright)
		light = 255;

	// Apply brightness
	else
		light = MathStuff::clamp(light * render_3d_brightness, 0, 255);

	// If we have a non-coloured light, darken it a bit to
	// closer resemble the software renderer light level
	float mult = (float)light / 255.0f;
	mult *= (mult * 1.3f);
	glColor4f(colour.fr()*mult, colour.fg()*mult, colour.fb()*mult, colour.fa()*alpha);
}

/* MapRenderer3D::setFog
 * Sets the OpenGL fog for rendering an object using [fogcol]
 *******************************************************************/
void MapRenderer3D::setFog(rgba_t &fogcol, uint8_t light)
{
	if (!fog)
		return;

	// Setup fog colour
	GLfloat fogColor[3] = { fogcol.fr(), fogcol.fg(), fogcol.fb() };
	if (fog_colour_last.r != fogcol.r || fog_colour_last.g != fogcol.g || fog_colour_last.b != fogcol.b)
	{
		glFogfv(GL_FOG_COLOR, fogColor);
		fog_colour_last = fogcol;
	}


	// Setup fog depth
	float depth;

	// check if fog color is default
	if (!render_fog_new_formula || (fogColor[0] == 0 && fogColor[1] == 0 && fogColor[2] == 0))
	{
		float lm = light / 170.0f;
		depth = (lm * lm * 3000.0f);
	}
	else
		depth = render_fog_distance;

	if (fog_depth_last != depth)
	{
		glFogf(GL_FOG_END, depth);
		fog_depth_last = depth;
	}
}

/* MapRenderer3D::renderMap
 * Renders the map in 3d
 *******************************************************************/
void MapRenderer3D::renderMap()
{
	// Setup GL stuff
	glEnable(GL_DEPTH_TEST);
	glCullFace(GL_BACK);
	glEnable(GL_CULL_FACE);
	glEnable(GL_ALPHA_TEST);
	glDepthMask(GL_TRUE);
	glAlphaFunc(GL_GREATER, 0.0f);

	// Init
	tex_last = NULL;

	// Create flats array if needed
	if (sector_flats.size() != map->nSectors())
		sector_flats.resize(map->nSectors());

	// Create lines array if empty
	if (lines.size() != map->nLines())
		lines.resize(map->nLines());

	// Create things array if empty
	if (things.size() != map->nThings())
		things.resize(map->nThings());

	// Init VBO stuff
	if (OpenGL::vboSupport())
	{
		// Check if any polygon vertex data has changed (in this case we need to refresh the entire vbo)
		bool vbo_updated = false;
		for (unsigned a = 0; a < map->nSectors(); a++)
		{
			Polygon2D* poly = map->getSector(a)->getPolygon();
			if (poly && poly->vboUpdate() > 1)
			{
				updateFlatsVBO();
				vbo_updated = true;
				break;
			}
		}

		// Create VBO if necessary
		if (!vbo_updated && vbo_flats == 0)
			updateFlatsVBO();

		glEnableClientState(GL_VERTEX_ARRAY);
		glEnableClientState(GL_TEXTURE_COORD_ARRAY);
	}

	// Quick distance vis check
	sf::Clock clock;
	quickVisDiscard();

	// Build lists of quads and flats to render
	checkVisibleFlats();
	checkVisibleQuads();

	// Render sky
	if (render_3d_sky)
		renderSky();
	OpenGL::setColour(COL_WHITE);

	if (fog)
	{
		glEnable(GL_FOG);

		glFogi(GL_FOG_MODE, GL_LINEAR);
		glFogf(GL_FOG_DENSITY, 1.0f);
		glFogf(GL_FOG_START, 0.0f);

		if (render_fog_quality)
			glHint(GL_FOG_HINT, GL_NICEST);
		else
			glHint(GL_FOG_HINT, GL_FASTEST);
	}

	// Render walls
	renderWalls();

	// Render flats
	renderFlats();

	// Render things
	if (render_3d_things > 0)
		renderThings();

	// Render transparent stuff
	renderTransparentWalls();

	// Check elapsed time
	if (render_max_dist_adaptive)
	{
		long ms = clock.getElapsedTime().asMilliseconds();
		if (ms > render_adaptive_ms)
		{
			render_max_dist = render_max_dist - 100;
			if (render_max_dist < 1000)
				render_max_dist = 1000;
		}
		else if (ms < render_adaptive_ms-5)
		{
			render_max_dist = render_max_dist + 100;
			if (render_max_dist > 20000)
				render_max_dist = 20000;
		}
	}

	// Cleanup gl state
	glDisable(GL_ALPHA_TEST);
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_CULL_FACE);
	glDisable(GL_FOG);
}

/* MapRenderer3D::renderSkySlice
 * Renders a cylindrical 'slice' of the sky between [top] and
 * [bottom] on the z axis
 *******************************************************************/
void MapRenderer3D::renderSkySlice(float top, float bottom, float atop, float abottom, float size, float tx, float ty)
{
	float tc_x = 0.0f;
	float tc_y1 = (-top + 1.0f) * (ty * 0.5f);
	float tc_y2 = (-bottom + 1.0f) * (ty * 0.5f);

	glBegin(GL_QUADS);

	// Go through circular points
	for (unsigned a = 0; a < 31; a++)
	{
		// Top
		glColor4f(1.0f, 1.0f, 1.0f, atop);
		glTexCoord2f(tc_x+tx, tc_y1);	glVertex3f(cam_position.x + (sky_circle[a+1].x * size), cam_position.y - (sky_circle[a+1].y * size), cam_position.z + (top * size));
		glTexCoord2f(tc_x, tc_y1);		glVertex3f(cam_position.x + (sky_circle[a].x * size), cam_position.y - (sky_circle[a].y * size), cam_position.z + (top * size));

		// Bottom
		glColor4f(1.0f, 1.0f, 1.0f, abottom);
		glTexCoord2f(tc_x, tc_y2);		glVertex3f(cam_position.x + (sky_circle[a].x * size), cam_position.y - (sky_circle[a].y * size), cam_position.z + (bottom * size));
		glTexCoord2f(tc_x+tx, tc_y2);	glVertex3f(cam_position.x + (sky_circle[a+1].x * size), cam_position.y - (sky_circle[a+1].y * size), cam_position.z + (bottom * size));

		tc_x += tx;
	}

	// Link last point -> first
	// Top
	glColor4f(1.0f, 1.0f, 1.0f, atop);
	glTexCoord2f(tc_x+tx, tc_y1);	glVertex3f(cam_position.x + (sky_circle[0].x * size), cam_position.y - (sky_circle[0].y * size), cam_position.z + (top * size));
	glTexCoord2f(tc_x, tc_y1);		glVertex3f(cam_position.x + (sky_circle[31].x * size), cam_position.y - (sky_circle[31].y * size), cam_position.z + (top * size));

	// Bottom
	glColor4f(1.0f, 1.0f, 1.0f, abottom);
	glTexCoord2f(tc_x, tc_y2);		glVertex3f(cam_position.x + (sky_circle[31].x * size), cam_position.y - (sky_circle[31].y * size), cam_position.z + (bottom * size));
	glTexCoord2f(tc_x+tx, tc_y2);	glVertex3f(cam_position.x + (sky_circle[0].x * size), cam_position.y - (sky_circle[0].y * size), cam_position.z + (bottom * size));

	glEnd();
}

/* MapRenderer3D::renderSky
 * Renders the sky
 *******************************************************************/
void MapRenderer3D::renderSky()
{
	OpenGL::setColour(COL_WHITE);
	glDisable(GL_CULL_FACE);
	glDisable(GL_FOG);
	glDisable(GL_DEPTH_TEST);
	glDepthMask(GL_FALSE);
	glEnable(GL_TEXTURE_2D);
	GLTexture* sky = NULL;

	// Center skybox a bit below the camera view
	glPushMatrix();
	glTranslatef(0.0f, 0.0f, -10.0f);

	// Get sky texture
	if (!skytex2.IsEmpty())
		sky = theMapEditor->textureManager().getTexture(skytex2, false);
	else
		sky = theMapEditor->textureManager().getTexture(skytex1, false);
	if (sky)
	{
		// Bind texture
		sky->bind();

		// Get average colour if needed
		if (skycol_top.a == 0)
		{
			int theight = sky->getHeight() * 0.4;
			skycol_top = sky->averageColour(rect_t(0, 0, sky->getWidth(), theight));
			skycol_bottom = sky->averageColour(rect_t(0, sky->getHeight() - theight, sky->getWidth(), sky->getHeight()));
		}

		// Render top cap
		float size = 64.0f;
		glDisable(GL_TEXTURE_2D);
		OpenGL::setColour(skycol_top, false);
		glBegin(GL_QUADS);
		glVertex3f(cam_position.x-(size*10), cam_position.y-(size*10), cam_position.z+size);
		glVertex3f(cam_position.x-(size*10), cam_position.y+(size*10), cam_position.z+size);
		glVertex3f(cam_position.x+(size*10), cam_position.y+(size*10), cam_position.z+size);
		glVertex3f(cam_position.x+(size*10), cam_position.y-(size*10), cam_position.z+size);
		glEnd();

		// Render bottom cap
		OpenGL::setColour(skycol_bottom, false);
		glBegin(GL_QUADS);
		glVertex3f(cam_position.x-(size*10), cam_position.y-(size*10), cam_position.z-size);
		glVertex3f(cam_position.x-(size*10), cam_position.y+(size*10), cam_position.z-size);
		glVertex3f(cam_position.x+(size*10), cam_position.y+(size*10), cam_position.z-size);
		glVertex3f(cam_position.x+(size*10), cam_position.y-(size*10), cam_position.z-size);
		glEnd();

		// Render skybox sides
		glDisable(GL_ALPHA_TEST);
		glEnable(GL_TEXTURE_2D);

		// Check for odd sky sizes
		float tx = 0.125f;
		float ty = 2.0f;
		if (sky->getWidth() > 256)
			tx = 0.125f / ((float)sky->getWidth() / 256.0f);
		if (sky->getHeight() > 128)
			ty = 1.0f;

		renderSkySlice(1.0f, 0.5f, 0.0f, 1.0f, size, tx, ty);	// Top
		renderSkySlice(0.5f, -0.5f, 1.0f, 1.0f, size, tx, ty);	// Middle
		renderSkySlice(-0.5f, -1.0f, 1.0f, 0.0f, size, tx, ty);	// Bottom
	}

	glPopMatrix();
	glDepthMask(GL_TRUE);
	glEnable(GL_CULL_FACE);
	glEnable(GL_DEPTH_TEST);
	glEnable(GL_ALPHA_TEST);

	// Render all sky quads
	glDisable(GL_TEXTURE_2D);
	for (unsigned a = 0; a < n_quads; a++)
	{
		// Ignore if not sky
		if ((quads[a]->flags & SKY) == 0)
			continue;

		// Render quad
		renderQuad(quads[a]);
		quads[a] = quads[n_quads-1];
		n_quads--;
		a--;
	}

	// Render all sky flats
	flat_last = 0;
	for (unsigned a = 0; a < n_flats; a++)
	{
		// Ignore if not sky
		if ((flats[a]->flags & SKY) == 0)
			continue;

		// Render quad
		renderFlat(flats[a]);
		flats[a] = flats[n_flats-1];
		n_flats--;
		a--;
	}
	glEnable(GL_TEXTURE_2D);
}

/* MapRenderer3D::updateFlatTexCoords
 * Updates the vertex texture coordinates of all polygons for sector
 * [index]
 *******************************************************************/
void MapRenderer3D::updateFlatTexCoords(unsigned index, unsigned flat_index)
{
	// Check index
	if (index >= map->nSectors())
		return;

	// Get sector
	MapSector* sector = map->getSector(index);

	// Get scaling/offset info
	double ox = 0;
	double oy = 0;
	// TODO 3dfloors
	double sx = sector_flats[index][flat_index].texture->getScaleX();
	double sy = sector_flats[index][flat_index].texture->getScaleY();
	double rot = 0;

	// Check for UDMF + ZDoom extensions
	if (theMapEditor->currentMapDesc().format == MAP_UDMF && S_CMPNOCASE(theGameConfiguration->udmfNamespace(), "zdoom"))
	{
		// TODO 3dfloors -- this should be taken from the control sector
		if (!(sector_flats[index][flat_index].flags & CEIL))
		{
			ox = sector->floatProperty("xpanningfloor");
			oy = sector->floatProperty("ypanningfloor");
			sx *= (1.0 / sector->floatProperty("xscalefloor"));
			sy *= (1.0 / sector->floatProperty("yscalefloor"));
			rot = sector->floatProperty("rotationfloor");
		}
		else
		{
			ox = sector->floatProperty("xpanningceiling");
			oy = sector->floatProperty("ypanningceiling");
			sx *= (1.0 / sector->floatProperty("xscaleceiling"));
			sy *= (1.0 / sector->floatProperty("yscaleceiling"));
			rot = sector->floatProperty("rotationceiling");
		}
	}

	// Scaling applies to offsets as well.
	// Note for posterity: worldpanning only applies to textures, not flats
	ox /= sx;
	oy /= sy;

	// Update polygon texture coordinates
	sector->getPolygon()->setTexture(sector_flats[index][flat_index].texture);
	sector->getPolygon()->updateTextureCoords(sx, sy, ox, oy, rot);
}

/* MapRenderer3D::updateSector
 * Updates cached rendering data for sector [index]
 *******************************************************************/
void MapRenderer3D::updateSector(unsigned index, bool update_vbo)
{
	// Check index
	if (index >= map->nSectors())
		return;

	MapSector* sector = map->getSector(index);
	sector_flats[index].resize(2 * (1 + sector->extra_floors.size()));

	// Update floor
	flat_3d_t& floor_flat = sector_flats[index][0];
	floor_flat.sector = sector;
	floor_flat.extra_floor_index = -1;
	floor_flat.texture = theMapEditor->textureManager().getFlat(sector->getFloorTex(), theGameConfiguration->mixTexFlats());
	floor_flat.colour = sector->getColour(1, true);
	floor_flat.fogcolour = sector->getFogColour();
	floor_flat.light = sector->getLight(1);
	floor_flat.flags = 0;
	floor_flat.plane = sector->getFloorPlane();
	floor_flat.base_alpha = 1.0f;
	if (S_CMPNOCASE(sector->getFloorTex(), theGameConfiguration->skyFlat()))
		floor_flat.flags |= SKY;

	// Update ceiling
	flat_3d_t& ceiling_flat = sector_flats[index][1];
	ceiling_flat.sector = sector;
	ceiling_flat.extra_floor_index = -1;
	ceiling_flat.texture = theMapEditor->textureManager().getFlat(sector->getCeilingTex(), theGameConfiguration->mixTexFlats());
	ceiling_flat.colour = sector->getColour(2, true);
	ceiling_flat.fogcolour = sector->getFogColour();
	ceiling_flat.light = sector->getLight(2);
	ceiling_flat.flags = CEIL;
	ceiling_flat.plane = sector->getCeilingPlane();
	ceiling_flat.base_alpha = 1.0f;
	if (S_CMPNOCASE(sector->getCeilingTex(), theGameConfiguration->skyFlat()))
		ceiling_flat.flags |= SKY;

	// Deal with 3D floors
	for (unsigned a = 0; a < sector->extra_floors.size(); a++)
	{
		extra_floor_t& extra = sector->extra_floors[a];
		MapSector* control_sector = map->getSector(extra.control_sector_index);

		// TODO which of these is really a floor and which is really a ceiling?
		// TODO BOTH sides of the inner flat are drawn sometimes!
		flat_3d_t& xf_floor = sector_flats[index][2 * (a + 1)];
		xf_floor.sector = sector;
		xf_floor.extra_floor_index = a;
		xf_floor.texture = theMapEditor->textureManager().getFlat(control_sector->getFloorTex(), theGameConfiguration->mixTexFlats());
		// TODO wrong.  maybe?  does it inherit from parent?
		xf_floor.colour = sector->getColour(1);
		// TODO again, maybe?
		xf_floor.fogcolour = sector->getFogColour();
		// TODO oughta support screen blends too!!
		// TODO this probably comes from the control sector, unless there's a flag, yadda...
		xf_floor.light = sector->getLight(0);
		xf_floor.flags = CEIL;
		if (extra.draw_inside)
			xf_floor.flags |= DRAWBOTH;
		xf_floor.plane = control_sector->getFloorPlane();
		xf_floor.base_alpha = extra.alpha;

		flat_3d_t& xf_ceiling = sector_flats[index][2 * (a + 1) + 1];
		xf_ceiling.sector = sector;
		xf_ceiling.extra_floor_index = a;
		xf_ceiling.texture = theMapEditor->textureManager().getFlat(control_sector->getCeilingTex(), theGameConfiguration->mixTexFlats());
		xf_ceiling.colour = sector->getColour(1);
		// TODO again, maybe?
		xf_ceiling.fogcolour = sector->getFogColour();
		// TODO this probably comes from the control sector, unless there's a flag, yadda...
		xf_ceiling.light = sector->getLight(0);
		xf_ceiling.flags = 0;
		if (extra.draw_inside)
			xf_ceiling.flags |= DRAWBOTH;
		xf_ceiling.plane = control_sector->getCeilingPlane();
		xf_ceiling.base_alpha = extra.alpha;
	}
	if (sector_flats[index].size() > 2)
		LOG_MESSAGE(2, "sector %d has %lu flats", index, sector_flats[index].size());

	// Update VBOs
	if (OpenGL::vboSupport() && update_vbo)
	{
		glBindBuffer(GL_ARRAY_BUFFER, vbo_flats);

		for (unsigned a = 0; a < sector_flats[index].size(); a++)
		{
			updateFlatTexCoords(index, a);
			Polygon2D::setupVBOPointers();
			sector->getPolygon()->setZ(sector_flats[index][a].plane);
			sector->getPolygon()->updateVBOData(sector_flats[index][a].vbo_offset);
		}

		glBindBuffer(GL_ARRAY_BUFFER, 0);
		sector->getPolygon()->setZ(0);
	}

	// Finish up
	for (unsigned a = 0; a < sector_flats[index].size(); a++)
		sector_flats[index][a].updated_time = theApp->runTimer();
}

/* MapRenderer3D::isSectorStale
 * Returns whether the sector at [index] needs to be updated.
 *******************************************************************/
bool MapRenderer3D::isSectorStale(unsigned index)
{
	MapSector* sector = map->getSector(index);
	if (!sector) return false;

	return (!sector_flats[index].size() ||
		sector_flats[index][0].updated_time < sector->modifiedTime() ||
		sector_flats[index][0].updated_time < sector->geometryUpdatedTime());
}

/* MapRenderer3D::renderFlat
 * Renders [flat]
 *******************************************************************/
void MapRenderer3D::renderFlat(flat_3d_t* flat)
{
	// Skip if no sector (for whatever reason)
	if (!flat->sector)
		return;

	// Setup special rendering options
	float alpha = flat->alpha * flat->base_alpha;
	if (flat->flags & SKY && render_3d_sky)
	{
		alpha = 0;
		glDisable(GL_ALPHA_TEST);
	}

	// Setup colour/light
	setLight(flat->colour, flat->light, alpha);

	// Setup fog colour
	setFog(flat->fogcolour, flat->light);

	// Render flat
	if (OpenGL::vboSupport() && flats_use_vbo)
	{
		// Setup for floor or ceiling
		if (flat->flags & CEIL)
		{
			if (flat_last != 2)
			{
				glCullFace(GL_BACK);
				glBindBuffer(GL_ARRAY_BUFFER, vbo_flats);
				Polygon2D::setupVBOPointers();
				flat_last = 2;
			}
		}
		else
		{
			if (flat_last != 1)
			{
				glCullFace(GL_FRONT);
				glBindBuffer(GL_ARRAY_BUFFER, vbo_flats);
				Polygon2D::setupVBOPointers();
				flat_last = 1;
			}
		}

		// Render
		flat->sector->getPolygon()->renderVBO(flat->vbo_offset);
	}
	else
	{
		glPushMatrix();

		// Setup for floor or ceiling
		if (flat->flags & CEIL)
		{
			glCullFace(GL_BACK);
			glTranslated(0, 0, flat->sector->getCeilingHeight());
		}
		else
		{
			glCullFace(GL_FRONT);
			glTranslated(0, 0, flat->sector->getFloorHeight());
		}

		// Render
		flat->sector->getPolygon()->render();

		glPopMatrix();
	}

	// Reset settings
	if (flat->flags & SKY && render_3d_sky)
		glEnable(GL_ALPHA_TEST);
}

/* MapRenderer3D::renderFlats
 * Renders all currently visible flats
 *******************************************************************/
void MapRenderer3D::renderFlats()
{
	// Check for map
	if (!map)
		return;

	// Init textures
	glEnable(GL_TEXTURE_2D);

	// Render all visible opaque flats, ordered by texture
	unsigned a = 0;
	flat_last = 0;
	while (n_flats > 0)
	{
		tex_last = NULL;
		a = 0;
		while (a < n_flats)
		{
			// Skip different textures on this loop, and save all translucent
			// flats for last
			if ((tex_last && flats[a]->texture != tex_last) ||
				flats[a]->base_alpha < 1.0f)
			{
				a++;
				continue;
			}

			// Check texture
			if (!tex_last)
				tex_last = flats[a]->texture;
			if (tex_last)
				flats[a]->texture->bind();

			// Render flat
			renderFlat(flats[a]);
			flats[a] = flats[n_flats-1];
			n_flats--;
		}
		if (tex_last == NULL)
			break;
	}

	// Render any remaining translucent flats, ordered by depth
	// TODO order by depth?  how??  sector distance, then plane height?  (note
	// that sloped and translucent is forbidden even in gzdoom)
	for (unsigned b = 0; b < n_flats; b++)
	{
		if (tex_last != flats[b]->texture)
		{
			tex_last = flats[b]->texture;
			tex_last->bind();
		}
		renderFlat(flats[b]);
	}
	n_flats = 0;

	// Reset gl stuff
	glDisable(GL_TEXTURE_2D);
	if (OpenGL::vboSupport())
	{
		glDisableClientState(GL_VERTEX_ARRAY);
		glDisableClientState(GL_TEXTURE_COORD_ARRAY);
		glBindBuffer(GL_ARRAY_BUFFER, 0);
	}
}

/* MapRenderer3D::renderFlatSelection
 * Renders selection overlay for all selected flats
 *******************************************************************/
void MapRenderer3D::renderFlatSelection(vector<selection_3d_t>& selection, float alpha)
{
	if (!render_selection)
		return;

	// Setup gl stuff
	glLineWidth(2.0f);
	glDisable(GL_TEXTURE_2D);
	glDisable(GL_FOG);
	glDisable(GL_DEPTH_TEST);
	glEnable(GL_LINE_SMOOTH);
	glEnable(GL_CULL_FACE);

	// Setup colour
	rgba_t col1 = ColourConfiguration::getColour("map_3d_selection");
	col1.a *= alpha;
	OpenGL::setColour(col1);
	rgba_t col2 = col1;
	col2.a *= 0.5;

	// Go through selection
	for (unsigned a = 0; a < selection.size(); a++)
	{
		selection_3d_t& hilight = selection[a];
		// TODO this code is awfully similar to the renderHilight code
		// Ignore if not a sector hilight
		if (hilight.type != MapEditor::SEL_CEILING && hilight.type != MapEditor::SEL_FLOOR)
			continue;

		// Get sector
		MapSector* sector = map->getSector(hilight.index);
		if (!sector)
			return;

		// Get plane
		plane_t plane;
		if (hilight.extra_floor_index >= 0 && hilight.extra_floor_index < sector->extra_floors.size())
		{
			extra_floor_t& extra = sector->extra_floors[hilight.extra_floor_index];
			MapSector* control_sector = map->getSector(extra.control_sector_index);
			if (!control_sector)
				return;

			// Planes are reversed for a 3D floor
			// TODO the DRAWBOTH hack makes the type wrong when you're inside
			// TODO not true for vavoom
			if (hilight.type == MapEditor::SEL_FLOOR)
				plane = control_sector->getCeilingPlane();
			else
				plane = control_sector->getFloorPlane();
		}
		else
		{
			if (hilight.type == MapEditor::SEL_FLOOR)
				plane = sector->getFloorPlane();
			else
				plane = sector->getCeilingPlane();
		}

		// Draw sector outline
		vector<MapLine*> lines;
		sector->getLines(lines);
		OpenGL::setColour(col1, false);
		glBegin(GL_LINES);
		for (unsigned l = 0; l < lines.size(); l++)
		{
			glVertex3d(lines[l]->x1(), lines[l]->y1(), plane.height_at(lines[l]->x1(), lines[l]->y1()));
			glVertex3d(lines[l]->x2(), lines[l]->y2(), plane.height_at(lines[l]->x2(), lines[l]->y2()));
		}
		glEnd();

		// Render fill
		OpenGL::setColour(col2, false);
		glDisable(GL_CULL_FACE);
		sector->getPolygon()->setZ(plane);
		sector->getPolygon()->render();
		sector->getPolygon()->setZ(0);
		glEnable(GL_CULL_FACE);
	}

	glCullFace(GL_BACK);
}

/* MapRenderer3D::setupQuad
 * Sets up coordinates for a quad
 *******************************************************************/
void MapRenderer3D::setupQuad(MapRenderer3D::quad_3d_t* quad, fseg2_t seg, double top, double bottom)
{
	// Left
	quad->points[0].x = quad->points[1].x = seg.x1();
	quad->points[0].y = quad->points[1].y = seg.y1();

	// Right
	quad->points[2].x = quad->points[3].x = seg.x2();
	quad->points[2].y = quad->points[3].y = seg.y2();

	// Top/bottom
	quad->points[0].z = quad->points[3].z = top;
	quad->points[1].z = quad->points[2].z = bottom;
}

/* MapRenderer3D::setupQuad
 * Sets up coordinates for a quad
 *******************************************************************/
void MapRenderer3D::setupQuad(MapRenderer3D::quad_3d_t* quad, fseg2_t seg, plane_t top, plane_t bottom)
{
	// Left
	quad->points[0].x = quad->points[1].x = seg.x1();
	quad->points[0].y = quad->points[1].y = seg.y1();

	// Right
	quad->points[2].x = quad->points[3].x = seg.x2();
	quad->points[2].y = quad->points[3].y = seg.y2();

	// Top/bottom
	quad->points[0].z = top.height_at(quad->points[0].x, quad->points[0].y);
	quad->points[1].z = bottom.height_at(quad->points[1].x, quad->points[1].y);
	quad->points[2].z = bottom.height_at(quad->points[2].x, quad->points[2].y);
	quad->points[3].z = top.height_at(quad->points[3].x, quad->points[3].y);
}

/* MapRenderer3D::setupQuadTexCoords
 * Calculates texture coordinates for a quad
 *******************************************************************/
void MapRenderer3D::setupQuadTexCoords(MapRenderer3D::quad_3d_t* quad, int length, double o_left, double o_top, double h_top, double h_bottom, bool pegbottom, double sx, double sy)
{
	// Check texture
	if (!quad->texture)
		return;

	// Determine integral height
	int height = MathStuff::round(h_top - h_bottom);

	// Initial offsets
	double y1 = o_top;
	double y2 = o_top + height;
	if (pegbottom)
	{
		y2 = o_top + quad->texture->getHeight();
		y1 = y2 - height;
	}

	sx *= quad->texture->getScaleX();
	sy *= quad->texture->getScaleY();
	double x_mult = 1.0 / (quad->texture->getWidth() * sx);
	double y_mult = 1.0 / (quad->texture->getHeight() * sy);

	// Set texture coordinates
	quad->points[0].tx = o_left * x_mult;
	quad->points[0].ty = (y1 * y_mult) + ((h_top - quad->points[0].z) * y_mult);
	quad->points[1].tx = o_left * x_mult;
	quad->points[1].ty = (y2 * y_mult) + ((h_bottom - quad->points[1].z) * y_mult);
	quad->points[2].tx = (o_left + length) * x_mult;
	quad->points[2].ty = (y2 * y_mult) + ((h_bottom - quad->points[2].z) * y_mult);
	quad->points[3].tx = (o_left + length) * x_mult;
	quad->points[3].ty = (y1 * y_mult) + ((h_top - quad->points[3].z) * y_mult);
}

// Helper for updateLine -- fetches the per-wall-section offset and scale and
// adjusts the existing offsets to match.
static inline void _apply_zdoom_per_section_offsets(MapSide* side, string section_name, double* xoff, double* yoff, double* sx, double* sy)
{
	if (side->hasProp("offsetx_" + section_name))
		*xoff += side->floatProperty("offsetx_" + section_name);
	if (side->hasProp("offsety_" + section_name))
		*yoff += side->floatProperty("offsety_" + section_name);

	if (side->hasProp("scalex_" + section_name))
		*sx = 1.0 / side->floatProperty("scalex_" + section_name);
	if (side->hasProp("scaley_" + section_name))
		*sy = 1.0 / side->floatProperty("scaley_" + section_name);

	*xoff *= *sx;
	*yoff *= *sy;
}

/* MapRenderer3D::updateLine
 * Updates cached rendering data for line [index]
 *******************************************************************/
void MapRenderer3D::updateLine(unsigned index)
{
	// Check index
	if (index > lines.size())
		return;

	// Clear current line data
	lines[index].quads.clear();

	// Skip invalid line
	MapLine* line = map->getLine(index);
	if (!line->s1())
		return;

	// Process line special
	map->mapSpecials()->processLineSpecial(line);

	// Get relevant line info
	int map_format = theMapEditor->currentMapDesc().format;
	bool upeg = theGameConfiguration->lineBasicFlagSet("dontpegtop", line, map_format);
	bool lpeg = theGameConfiguration->lineBasicFlagSet("dontpegbottom", line, map_format);
	double xoff, yoff, sx, sy;
	bool mixed = theGameConfiguration->mixTexFlats();
	lines[index].line = line;
	double alpha = 1.0;
	if (line->hasProp("alpha"))
		alpha = line->floatProperty("alpha");

	// Get first side info
	int floor1 = line->frontSector()->getFloorHeight();
	int ceiling1 = line->frontSector()->getCeilingHeight();
	plane_t fp1 = line->frontSector()->getFloorPlane();
	plane_t cp1 = line->frontSector()->getCeilingPlane();
	rgba_t colour1 = line->frontSector()->getColour(0, true);
	rgba_t fogcolour1 = line->frontSector()->getFogColour();
	int light1 = line->s1()->getLight();
	int xoff1 = line->s1()->getOffsetX();
	int yoff1 = line->s1()->getOffsetY();

	if (render_shade_orthogonal_lines)
	{
		// Increase light level for N/S facing lines
		if (line->x1() == line->x2())
		{
			colour1.r = MathStuff::clamp(colour1.r + 16, 0, 255);
			colour1.g = MathStuff::clamp(colour1.g + 16, 0, 255);
			colour1.b = MathStuff::clamp(colour1.b + 16, 0, 255);
			light1 = MathStuff::clamp(light1 + 16, 0, 255);
		}

		// Decrease light level for E/W facing lines
		if (line->y1() == line->y2())
		{
			colour1.r = MathStuff::clamp(colour1.r - 16, 0, 255);
			colour1.g = MathStuff::clamp(colour1.g - 16, 0, 255);
			colour1.b = MathStuff::clamp(colour1.b - 16, 0, 255);
			light1 = MathStuff::clamp(light1 - 16, 0, 255);
		}
	}

	// --- One-sided line ---
	int length = MathStuff::round(line->getLength());
	if (line->s1() && !line->s2())
	{
		quad_3d_t quad;

		// Determine offsets and scale
		xoff = xoff1;
		yoff = yoff1;
		sx = sy = 1;
		if (udmf_zdoom)
			_apply_zdoom_per_section_offsets(line->s1(), "mid", &xoff, &yoff, &sx, &sy);

		// Create quad
		setupQuad(&quad, line->seg(), cp1, fp1);
		quad.colour = colour1;
		quad.fogcolour = fogcolour1;
		quad.light = light1;
		quad.texture = theMapEditor->textureManager().getTexture(line->s1()->getTexMiddle(), mixed);
		setupQuadTexCoords(&quad, length, xoff, yoff, ceiling1, floor1, lpeg, sx, sy);

		// Add middle quad and finish
		lines[index].quads.push_back(quad);
		lines[index].updated_time = theApp->runTimer();
		return;
	}

	// --- Two-sided line ---

	// TODO middle parts may show 3D floors!

	// Get second side info
	int floor2 = line->backSector()->getFloorHeight();
	int ceiling2 = line->backSector()->getCeilingHeight();
	plane_t fp2 = line->backSector()->getFloorPlane();
	plane_t cp2 = line->backSector()->getCeilingPlane();
	rgba_t colour2 = line->backSector()->getColour(0, true);
	rgba_t fogcolour2 = line->backSector()->getFogColour();
	int light2 = line->s2()->getLight();
	int xoff2 = line->s2()->getOffsetX();
	int yoff2 = line->s2()->getOffsetY();
	int lowceil = min(ceiling1, ceiling2);
	int highfloor = max(floor1, floor2);
	string sky_flat = theGameConfiguration->skyFlat();
	string hidden_tex = map->currentFormat() == MAP_DOOM64 ? "?" : "-";
	bool show_midtex = (map->currentFormat() != MAP_DOOM64) || (line->intProperty("flags") & 512);
	// Heights at both endpoints, for both planes, on both sides
	double f1h1 = fp1.height_at(line->x1(), line->y1());
	double f1h2 = fp1.height_at(line->x2(), line->y2());
	double f2h1 = fp2.height_at(line->x1(), line->y1());
	double f2h2 = fp2.height_at(line->x2(), line->y2());
	double c1h1 = cp1.height_at(line->x1(), line->y1());
	double c1h2 = cp1.height_at(line->x2(), line->y2());
	double c2h1 = cp2.height_at(line->x1(), line->y1());
	double c2h2 = cp2.height_at(line->x2(), line->y2());

	if (render_shade_orthogonal_lines)
	{
		// Increase light level for N/S facing lines
		if (line->x1() == line->x2())
		{
			colour2.r = MathStuff::clamp(colour2.r + 16, 0, 255);
			colour2.g = MathStuff::clamp(colour2.g + 16, 0, 255);
			colour2.b = MathStuff::clamp(colour2.b + 16, 0, 255);
			light2 = MathStuff::clamp(light2 + 16, 0, 255);
		}

		// Decrease light level for E/W facing lines
		if (line->y1() == line->y2())
		{
			colour2.r = MathStuff::clamp(colour2.r - 16, 0, 255);
			colour2.g = MathStuff::clamp(colour2.g - 16, 0, 255);
			colour2.b = MathStuff::clamp(colour2.b - 16, 0, 255);
			light2 = MathStuff::clamp(light2 - 16, 0, 255);
		}
	}

	// Front lower
	if (f2h1 > f1h1 || f2h2 > f1h2)
	{
		quad_3d_t quad;

		// Determine offsets and scale
		xoff = xoff1;
		yoff = yoff1;
		sx = sy = 1;
		if (udmf_zdoom)
			_apply_zdoom_per_section_offsets(line->s1(), "bottom", &xoff, &yoff, &sx, &sy);

		if (lpeg)	// Lower unpegged
			yoff += (ceiling1 - floor2);

		// Create quad
		setupQuad(&quad, line->seg(), fp2, fp1);
		quad.colour = colour1;
		quad.fogcolour = fogcolour1;
		quad.light = light1;
		quad.texture = theMapEditor->textureManager().getTexture(line->s1()->getTexLower(), mixed);
		setupQuadTexCoords(&quad, length, xoff, yoff, floor2, floor1, false, sx, sy);
		// No, the sky hack is only for ceilings!
		// if (S_CMPNOCASE(sky_flat, line->backSector()->getFloorTex())) quad.flags |= SKY;
		quad.flags |= LOWER;

		// Add quad
		lines[index].quads.push_back(quad);
	}

	// Front middle
	string midtex1 = line->stringProperty("side1.texturemiddle");
	if (!midtex1.IsEmpty() && midtex1 != hidden_tex && show_midtex)
	{
		quad_3d_t quad;

		// Get texture
		quad.texture = theMapEditor->textureManager().getTexture(midtex1, mixed);

		// Determine offsets and scale
		xoff = xoff1;
		yoff = yoff1;
		sx = sy = 1;
		if (udmf_zdoom)
			_apply_zdoom_per_section_offsets(line->s1(), "mid", &xoff, &yoff, &sx, &sy);

		// Setup quad coordinates
		double ytex = 0;
		double top, bottom;
		if ((map->currentFormat() == MAP_DOOM64) || (udmf_zdoom && line->boolProperty("wrapmidtex")))
		{
			top = lowceil;
			bottom = highfloor;
			ytex = yoff;
			if (lpeg)
				ytex -= lowceil - highfloor;
		}
		else if (lpeg)
		{
			bottom = highfloor + yoff;
			top = bottom + (quad.texture->getHeight() * sy);
		}
		else
		{
			top = lowceil + yoff;
			bottom = top - (quad.texture->getHeight() * sy);
		}
		// The "correct" thing here is to allow textures to run into the floor
		// unless clipmidtex is on, but OpenGL is designed not to allow that to
		// happen, and GZDoom doesn't support it either.
		if (bottom < highfloor)
			bottom = highfloor;
		if (top > lowceil)
		{
			ytex = top - lowceil;
			top = lowceil;
		}

		// Create quad
		setupQuad(&quad, line->seg(), top, bottom);
		quad.colour = colour1.ampf(1.0f, 1.0f, 1.0f, alpha);
		quad.fogcolour = fogcolour1;
		quad.light = light1;
		setupQuadTexCoords(&quad, length, xoff, ytex, top, bottom, false, sx, sy);
		quad.flags |= MIDTEX;
		if (line->hasProp("renderstyle") && !wxStrcmp(line->stringProperty("renderstyle"), "add"))
			quad.flags |= TRANSADD;

		// Add quad
		lines[index].quads.push_back(quad);
	}

	// Front upper
	if (c1h1 > c2h1 || c1h2 > c2h2)
	{
		quad_3d_t quad;

		// Determine offsets and scale
		xoff = xoff1;
		yoff = yoff1;
		sx = sy = 1;
		if (udmf_zdoom)
			_apply_zdoom_per_section_offsets(line->s1(), "top", &xoff, &yoff, &sx, &sy);

		// Create quad
		setupQuad(&quad, line->seg(), cp1, cp2);
		quad.colour = colour1;
		quad.fogcolour = fogcolour1;
		quad.light = light1;
		quad.texture = theMapEditor->textureManager().getTexture(line->s1()->getTexUpper(), mixed);
		setupQuadTexCoords(&quad, length, xoff, yoff, ceiling1, ceiling2, !upeg, sx, sy);
		// Sky hack only applies if both sectors have a sky ceiling
		if (S_CMPNOCASE(sky_flat, line->frontSector()->getCeilingTex()) && S_CMPNOCASE(sky_flat, line->backSector()->getCeilingTex())) quad.flags |= SKY;
		quad.flags |= UPPER;

		// Add quad
		lines[index].quads.push_back(quad);
	}

	// Back lower
	if (f1h1 > f2h1 || f1h2 > f2h2)
	{
		quad_3d_t quad;

		// Determine offsets and scale
		xoff = xoff2;
		yoff = yoff2;
		sx = sy = 1;
		if (udmf_zdoom)
			_apply_zdoom_per_section_offsets(line->s2(), "bottom", &xoff, &yoff, &sx, &sy);

		if (lpeg)	// Lower unpegged
			yoff += (ceiling2 - floor1);

		// Create quad
		setupQuad(&quad, line->seg().flip(), fp1, fp2);
		quad.colour = colour2;
		quad.fogcolour = fogcolour2;
		quad.light = light2;
		quad.texture = theMapEditor->textureManager().getTexture(line->s2()->getTexLower(), mixed);
		setupQuadTexCoords(&quad, length, xoff, yoff, floor1, floor2, false, sx, sy);
		if (S_CMPNOCASE(sky_flat, line->frontSector()->getFloorTex())) quad.flags |= SKY;
		quad.flags |= BACK;
		quad.flags |= LOWER;

		// Add quad
		lines[index].quads.push_back(quad);
	}

	// Back middle
	string midtex2 = line->stringProperty("side2.texturemiddle");
	if (!midtex2.IsEmpty() && midtex2 != hidden_tex && show_midtex)
	{
		quad_3d_t quad;

		// Get texture
		quad.texture = theMapEditor->textureManager().getTexture(midtex2, mixed);

		// Determine offsets and scale
		xoff = xoff2;
		yoff = yoff2;
		sx = sy = 1;
		if (udmf_zdoom)
			_apply_zdoom_per_section_offsets(line->s2(), "mid", &xoff, &yoff, &sx, &sy);

		// Setup quad coordinates
		double ytex = 0;
		double top, bottom;
		if ((map->currentFormat() == MAP_DOOM64) || (udmf_zdoom && line->boolProperty("wrapmidtex")))
		{
			top = lowceil;
			bottom = highfloor;
			ytex = yoff;
			if (lpeg)
				ytex -= lowceil - highfloor;
		}
		else if (lpeg)
		{
			bottom = highfloor + yoff;
			top = bottom + (quad.texture->getHeight() * sy);
		}
		else
		{
			top = lowceil + yoff;
			bottom = top - (quad.texture->getHeight() * sy);
		}
		// The "correct" thing here is to allow textures to run into the floor
		// unless clipmidtex is on, but OpenGL is designed not to allow that to
		// happen, and GZDoom doesn't support it either.
		if (bottom < highfloor)
			bottom = highfloor;
		if (top > lowceil)
		{
			ytex = top - lowceil;
			top = lowceil;
		}

		// Create quad
		setupQuad(&quad, line->seg().flip(), top, bottom);
		quad.colour = colour2.ampf(1.0f, 1.0f, 1.0f, alpha);
		quad.fogcolour = fogcolour2;
		quad.light = light2;
		setupQuadTexCoords(&quad, length, xoff, ytex, top, bottom, false, sx, sy);
		quad.flags |= BACK;
		quad.flags |= MIDTEX;
		if (line->hasProp("renderstyle") && !wxStrcmp(line->stringProperty("renderstyle"), "add"))
			quad.flags |= TRANSADD;

		// Add quad
		lines[index].quads.push_back(quad);
	}

	// Back upper
	if (c2h1 > c1h1 || c2h2 > c1h2)
	{
		quad_3d_t quad;

		// Determine offsets and scale
		xoff = xoff2;
		yoff = yoff2;
		sx = sy = 1;
		if (udmf_zdoom)
			_apply_zdoom_per_section_offsets(line->s2(), "top", &xoff, &yoff, &sx, &sy);

		// Create quad
		setupQuad(&quad, line->seg().flip(), cp2, cp1);
		quad.colour = colour2;
		quad.fogcolour = fogcolour2;
		quad.light = light2;
		quad.texture = theMapEditor->textureManager().getTexture(line->s2()->getTexUpper(), mixed);
		setupQuadTexCoords(&quad, length, xoff, yoff, ceiling2, ceiling1, !upeg, sx, sy);
		if (S_CMPNOCASE(sky_flat, line->frontSector()->getCeilingTex())) quad.flags |= SKY;
		quad.flags |= BACK;
		quad.flags |= UPPER;

		// Add quad
		lines[index].quads.push_back(quad);
	}

	// Add any middle lines created by 3D floors
	// TODO this code is so, so duplicated, oh dear
	// TODO going to have to deal with vertical overlaps (which already do
	// weird things when sloped floors meet in the middle of a line)
	// TODO relatedly, need to be able to SPLIT a quad even when there's no
	// texture, because 3d floors can change the lighting
	// TODO will one-sided lines ever have to worry about this?
	// TODO need to do back sector too, but with some of the logic reversed
	MapSector* sectors[2] = { line->backSector(), line->frontSector() };
	for (unsigned front = 0; front < 2; front++)
	{
		MapSector* sector = sectors[front];
		for (unsigned a = 0; a < sector->extra_floors.size(); a++)
		{
			extra_floor_t& extra = sector->extra_floors[a];
			MapSector* control_sector = map->getSector(extra.control_sector_index);
			MapLine* control_line = map->getLine(extra.control_line_index);

			// TODO
			xoff = yoff = 0;
			sx = sy = 1;

			// TODO there's a flag for showing on both sides
			// TODO and flags for switching out where the texture comes from
			string texname = control_line->s1()->getTexMiddle();

			quad_3d_t quad;
			fseg2_t seg = line->seg();
			// If the 3D floor is on the front side of the line, then the
			// outside wall is on the back, so the line segment must be flipped
			if (front)
				seg = seg.flip();
			setupQuad(&quad, seg, control_sector->getCeilingPlane(), control_sector->getFloorPlane());
			// TODO all this nonsense
			quad.colour = colour1.ampf(1.0f, 1.0f, 1.0f, extra.alpha);
			quad.fogcolour = fogcolour1;
			quad.light = light1;

			quad.texture = theMapEditor->textureManager().getTexture(texname, mixed);

			setupQuadTexCoords(&quad, length, xoff, yoff, control_sector->getCeilingHeight(), control_sector->getFloorHeight(), false, sx, sy);
			quad.flags |= MIDTEX;
			// TODO other flags?
			// TODO probably need to remember which extra-floor this came from
			// too, which is slightly more complicated since it might be the
			// sector on either side of the line
			// TODO TRANSADD if that one flag is set!

			lines[index].quads.push_back(quad);
		}
	}

	// Finished
	lines[index].updated_time = theApp->runTimer();
}

/* MapRenderer3D::renderQuad
 * Renders [quad]
 *******************************************************************/
void MapRenderer3D::renderQuad(MapRenderer3D::quad_3d_t* quad, float alpha)
{
	// Setup special rendering options
	if (quad->colour.a == 255)
	{
		if (quad->flags & SKY && render_3d_sky)
		{
			alpha = 0;
			glDisable(GL_ALPHA_TEST);
		}
		else if (quad->flags & MIDTEX)
			glAlphaFunc(GL_GREATER, 0.9f*alpha);
	}

	// Checking for additive renderstyle
	if (quad->flags & TRANSADD)
		glBlendFunc(GL_SRC_ALPHA, GL_ONE);
	else
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	// Setup colour/light
	setLight(quad->colour, quad->light, alpha);

	// Setup fog
	setFog(quad->fogcolour, quad->light);

	// Draw quad
	glBegin(GL_QUADS);
	glTexCoord2f(quad->points[0].tx, quad->points[0].ty);	glVertex3f(quad->points[0].x, quad->points[0].y, quad->points[0].z);
	glTexCoord2f(quad->points[1].tx, quad->points[1].ty);	glVertex3f(quad->points[1].x, quad->points[1].y, quad->points[1].z);
	glTexCoord2f(quad->points[2].tx, quad->points[2].ty);	glVertex3f(quad->points[2].x, quad->points[2].y, quad->points[2].z);
	glTexCoord2f(quad->points[3].tx, quad->points[3].ty);	glVertex3f(quad->points[3].x, quad->points[3].y, quad->points[3].z);
	glEnd();

	// Reset settings
	if (quad->colour.a == 255)
	{
		if (quad->flags & SKY && render_3d_sky)
			glEnable(GL_ALPHA_TEST);
		else if (quad->flags & MIDTEX)
			glAlphaFunc(GL_GREATER, 0.0f);
	}
}

/* MapRenderer3D::renderWalls
 * Renders all currently visible wall quads
 *******************************************************************/
void MapRenderer3D::renderWalls()
{
	// Init
	quads_transparent.clear();
	glEnable(GL_TEXTURE_2D);
	glCullFace(GL_BACK);

	// Render all visible quads, ordered by texture
	unsigned a = 0;
	while (n_quads > 0)
	{
		tex_last = NULL;
		a = 0;
		while (a < n_quads)
		{
			// Check alpha
			if (quads[a]->colour.a < 255)
			{
				quads_transparent.push_back(quads[a]);
				quads[a] = quads[n_quads-1];
				n_quads--;
				continue;
			}

			// Check texture
			if (!tex_last && quads[a]->texture)
			{
				tex_last = quads[a]->texture;
				quads[a]->texture->bind();
			}
			if (quads[a]->texture != tex_last)
			{
				a++;
				continue;
			}

			// Render quad
			renderQuad(quads[a], quads[a]->alpha);
			quads[a] = quads[n_quads-1];
			n_quads--;
		}
	}

	glDisable(GL_TEXTURE_2D);
}

/* MapRenderer3D::renderTransparentWalls
 * Renders all currently visible transparent wall quads
 *******************************************************************/
void MapRenderer3D::renderTransparentWalls()
{
	// Init
	glEnable(GL_TEXTURE_2D);
	glDepthMask(GL_FALSE);
	glDisable(GL_ALPHA_TEST);
	glCullFace(GL_BACK);

	// Render all transparent quads
	tex_last = NULL;
	for (unsigned a = 0; a < quads_transparent.size(); a++)
	{
		// Check texture
		if (quads_transparent[a]->texture != tex_last)
		{
			tex_last = quads_transparent[a]->texture;
			quads_transparent[a]->texture->bind();
		}

		// Render quad
		renderQuad(quads_transparent[a], quads_transparent[a]->alpha);
	}

	glDisable(GL_TEXTURE_2D);
	glDepthMask(GL_TRUE);
	glEnable(GL_ALPHA_TEST);
}

/* MapRenderer3D::renderWallSelection
 * Renders selection overlay for all selected wall quads
 *******************************************************************/
void MapRenderer3D::renderWallSelection(vector<selection_3d_t>& selection, float alpha)
{
	if (!render_selection)
		return;

	// Setup gl stuff
	glLineWidth(2.0f);
	glDisable(GL_TEXTURE_2D);
	glDisable(GL_FOG);
	glDisable(GL_DEPTH_TEST);
	glEnable(GL_LINE_SMOOTH);
	glEnable(GL_CULL_FACE);
	glCullFace(GL_BACK);

	// Setup colour
	rgba_t col1 = ColourConfiguration::getColour("map_3d_selection");
	col1.a *= alpha;
	OpenGL::setColour(col1);
	rgba_t col2 = col1;
	col2.a *= 0.5;

	// Go through selection
	for (unsigned a = 0; a < selection.size(); a++)
	{
		// Ignore if not a wall selection
		if (selection[a].type != MapEditor::SEL_SIDE_BOTTOM &&
		        selection[a].type != MapEditor::SEL_SIDE_MIDDLE &&
		        selection[a].type != MapEditor::SEL_SIDE_TOP)
			continue;

		// Get side
		MapSide* side = map->getSide(selection[a].index);
		if (!side)
			continue;

		// Get parent line index
		int line = map->getSide(selection[a].index)->getParentLine()->getIndex();

		// Get appropriate quad
		quad_3d_t* quad = NULL;
		for (unsigned q = 0; q < lines[line].quads.size(); q++)
		{
			// Check quad is correct side
			if (map->getLine(line)->s1() == side && lines[line].quads[q].flags & BACK)
				continue;
			if (map->getLine(line)->s2() == side && (lines[line].quads[q].flags & BACK) == 0)
				continue;

			// Check quad is correct part
			if (lines[line].quads[q].flags & UPPER)
			{
				if (selection[a].type == MapEditor::SEL_SIDE_TOP)
				{
					quad = &lines[line].quads[q];
					break;
				}
			}
			else if (lines[line].quads[q].flags & LOWER)
			{
				if (selection[a].type == MapEditor::SEL_SIDE_BOTTOM)
				{
					quad = &lines[line].quads[q];
					break;
				}
			}
			else if (selection[a].type == MapEditor::SEL_SIDE_MIDDLE)
			{
				quad = &lines[line].quads[q];
				break;
			}
		}

		if (!quad)
			continue;

		// Render quad outline
		OpenGL::setColour(col1, false);
		glBegin(GL_LINE_LOOP);
		for (unsigned v = 0; v < 4; v++)
			glVertex3f(quad->points[v].x, quad->points[v].y, quad->points[v].z);
		glEnd();

		// Render quad fill
		OpenGL::setColour(col2, false);
		glBegin(GL_QUADS);
		for (unsigned v = 0; v < 4; v++)
			glVertex3f(quad->points[v].x, quad->points[v].y, quad->points[v].z);
		glEnd();
	}
}

/* MapRenderer3D::updateThing
 * Updates cached data for [thing] (at [index])
 *******************************************************************/
void MapRenderer3D::updateThing(unsigned index, MapThing* thing)
{
	// Check index
	if (index >= things.size() || !thing)
		return;

	// Setup thing info
	things[index].type = theGameConfiguration->thingType(thing->getType());
	things[index].sector = map->getSector(map->sectorAt(thing->point()));

	// Get sprite texture
	uint32_t theight = render_thing_icon_size;
	things[index].sprite = theMapEditor->textureManager().getSprite(things[index].type->getSprite(), things[index].type->getTranslation(), things[index].type->getPalette());
	if (!things[index].sprite)
	{
		// Sprite not found, try an icon
		if (use_zeth_icons && things[index].type->getZeth() >= 0)
		{
			things[index].sprite = theMapEditor->textureManager().getEditorImage(S_FMT("zethicons/zeth%02d", things[index].type->getZeth()));
			things[index].flags |= ZETH;
		}
		if (!things[index].sprite)
			things[index].sprite = theMapEditor->textureManager().getEditorImage(S_FMT("thing/%s", things[index].type->getIcon()));
		things[index].flags |= ICON;
	}
	else theight = things[index].type->getScaleY() * things[index].sprite->getHeight();
	if (!things[index].sprite)
	{
		// Icon not found either, use unknown icon
		things[index].sprite = theMapEditor->textureManager().getEditorImage("thing/unknown");
	}

	// Determine z position
	if (things[index].sector)
	{
		// Get sector floor (or ceiling) height
		int sheight;
		float zheight = thing->floatProperty("height");
		if (things[index].type->isHanging())
		{
			sheight = things[index].sector->getCeilingPlane().height_at(thing->xPos(), thing->yPos());
			sheight -= theight;
			zheight = -zheight;
		}
		else
		{
			sheight = things[index].sector->getFloorPlane().height_at(thing->xPos(), thing->yPos());
		}

		// Set height
		things[index].z = sheight;
		if (things[index].type->shrinkOnZoom())
			things[index].z -= render_thing_icon_size*0.5;
		if (things[index].z < sheight)
			things[index].z = sheight;
		things[index].z += zheight;
	}

	// Adjust height by sprite Y offset if needed
	things[index].z += theMapEditor->textureManager().getVerticalOffset(things[index].type->getSprite());

	things[index].updated_time = theApp->runTimer();
}

/* MapRenderer3D::renderThings
 * Renders all currently visible things
 *******************************************************************/
void MapRenderer3D::renderThings()
{
	// Init
	glEnable(GL_TEXTURE_2D);
	glCullFace(GL_BACK);
	GLTexture* tex = NULL;

	// Go through things
	double dist, halfwidth, theight;
	double mdist = render_max_thing_dist;
	if (mdist <= 0 || mdist > render_max_dist) mdist = render_max_dist;
	rgba_t col;
	uint8_t light;
	float x1, y1, x2, y2;
	unsigned update = 0;
	fseg2_t strafe(cam_position.get2d(), (cam_position + cam_strafe).get2d());
	for (unsigned a = 0; a < map->nThings(); a++)
	{
		MapThing* thing = map->getThing(a);
		things[a].flags = things[a].flags & ~DRAWN;

		// Check side of camera
		if (cam_pitch > -0.9 && cam_pitch < 0.9)
		{
			if (MathStuff::lineSide(thing->point(), strafe) > 0)
				continue;
		}

		// Check thing distance if needed
		dist = MathStuff::distance(cam_position.get2d(), thing->point());
		if (mdist > 0 && dist > mdist)
			continue;

		// Update thing if needed
		if (things[a].updated_time < thing->modifiedTime() ||
		        (things[a].sector && (
				things[a].updated_time < things[a].sector->modifiedTime() ||
				things[a].updated_time < things[a].sector->geometryUpdatedTime())))
		{
			updateThing(a, thing);
			update++;
			if (update > 500)
				break;
		}

		// Skip if not shown
		if (!things[a].type->isDecoration() && render_3d_things == 2)
			continue;

		// Get thing sprite
		tex = things[a].sprite;

		// Bind texture if needed
		if (tex != tex_last)
		{
			tex->bind();
			tex_last = tex;
		}

		// Determine coordinates
		halfwidth = things[a].type->getScaleX() * tex->getWidth() * 0.5;
		theight = things[a].type->getScaleY() * tex->getHeight();
		if (things[a].flags & ICON)
		{
			halfwidth = render_thing_icon_size*0.5;
			theight = render_thing_icon_size;
		}
		x1 = thing->xPos() - cam_strafe.x * halfwidth;
		y1 = thing->yPos() - cam_strafe.y * halfwidth;
		x2 = thing->xPos() + cam_strafe.x * halfwidth;
		y2 = thing->yPos() + cam_strafe.y * halfwidth;
		things[a].height = theight;

		// Set colour/brightness
		light = 255;
		// If a thing is defined as fullbright but the sprite is missing,
		// we'll fallback on the icon, which needs to be colored as appropriate.
		if (things[a].type->isFullbright() && !(things[a].flags & ICON))
			col.set(255, 255, 255, 255);
		else
		{
			// Get light level from sector
			if (things[a].sector)
				light = things[a].sector->getLight();

			// Icon, use thing icon colour (not for Zeth icons, though)
			if (things[a].flags & ICON)
			{
				if (things[a].flags & ZETH)
					col.set(255, 255, 255, 255);
				else
					col.set(things[a].type->getColour());
			}

			// Otherwise use sector colour
			else if (things[a].sector)
				col.set(things[a].sector->getColour(0, true));
		}
		setLight(col, light, calcDistFade(dist, mdist));
		rgba_t fogcol = rgba_t(0, 0, 0, 0);
		if (things[a].sector)
			fogcol = things[a].sector->getFogColour();

		setFog(fogcol, light);

		// Draw thing
		glBegin(GL_QUADS);
		glTexCoord2f(0.0f, 0.0f);	glVertex3f(x1, y1, things[a].z + theight);
		glTexCoord2f(0.0f, 1.0f);	glVertex3f(x1, y1, things[a].z);
		glTexCoord2f(1.0f, 1.0f);	glVertex3f(x2, y2, things[a].z);
		glTexCoord2f(1.0f, 0.0f);	glVertex3f(x2, y2, things[a].z + theight);
		glEnd();

		things[a].flags |= DRAWN;
	}

	// Draw thing borders if needed
	if (render_3d_things_style >= 1)
	{
		glDisable(GL_TEXTURE_2D);
		glDepthMask(GL_FALSE);
		glAlphaFunc(GL_GREATER, 0.2f);
		glDisable(GL_CULL_FACE);
		glLineWidth(3.5f);

		for (unsigned a = 0; a < map->nThings(); a++)
		{
			// Skip if hidden
			if (!(things[a].flags & DRAWN))
				continue;

			MapThing* thing = map->getThing(a);
			col.set(things[a].type->getColour());
			float radius = things[a].type->getRadius();
			float bottom = things[a].z + 0.5f;
			float top = things[a].z;
			if (things[a].type->getHeight() < 0)
				top += things[a].height;
			else
				top += things[a].type->getHeight();

			// Fill
			glColor4f(col.fr(), col.fg(), col.fb(), 0.21f);
			uint8_t light2 = 255;
			rgba_t fogcol2 = rgba_t(0, 0, 0, 0);
			if (things[a].sector)
			{
				light2 = things[a].sector->getLight();
				fogcol2 = things[a].sector->getFogColour();
			}
			setFog(fogcol2, light2);
			glBegin(GL_QUADS);
			// Bottom
			glVertex3f(thing->xPos() - radius, thing->yPos() - radius, bottom);
			glVertex3f(thing->xPos() + radius, thing->yPos() - radius, bottom);
			glVertex3f(thing->xPos() + radius, thing->yPos() + radius, bottom);
			glVertex3f(thing->xPos() - radius, thing->yPos() + radius, bottom);
			if (render_3d_things_style == 2)
			{
				// Top
				glVertex3f(thing->xPos() + radius, thing->yPos() - radius, top);
				glVertex3f(thing->xPos() - radius, thing->yPos() - radius, top);
				glVertex3f(thing->xPos() - radius, thing->yPos() + radius, top);
				glVertex3f(thing->xPos() + radius, thing->yPos() + radius, top);
				// North
				glVertex3f(thing->xPos() - radius, thing->yPos() - radius, top);
				glVertex3f(thing->xPos() - radius, thing->yPos() - radius, bottom);
				glVertex3f(thing->xPos() + radius, thing->yPos() - radius, bottom);
				glVertex3f(thing->xPos() + radius, thing->yPos() - radius, top);
				// South
				glVertex3f(thing->xPos() + radius, thing->yPos() + radius, top);
				glVertex3f(thing->xPos() + radius, thing->yPos() + radius, bottom);
				glVertex3f(thing->xPos() - radius, thing->yPos() + radius, bottom);
				glVertex3f(thing->xPos() - radius, thing->yPos() + radius, top);
				// East
				glVertex3f(thing->xPos() + radius, thing->yPos() - radius, top);
				glVertex3f(thing->xPos() + radius, thing->yPos() - radius, bottom);
				glVertex3f(thing->xPos() + radius, thing->yPos() + radius, bottom);
				glVertex3f(thing->xPos() + radius, thing->yPos() + radius, top);
				// West
				glVertex3f(thing->xPos() - radius, thing->yPos() + radius, top);
				glVertex3f(thing->xPos() - radius, thing->yPos() + radius, bottom);
				glVertex3f(thing->xPos() - radius, thing->yPos() - radius, bottom);
				glVertex3f(thing->xPos() - radius, thing->yPos() - radius, top);
			}
			glEnd();

			// Outline
			glColor4f(col.fr(), col.fg(), col.fb(), 0.6f);
			// Bottom
			glBegin(GL_LINE_LOOP);
			glVertex3f(thing->xPos() - radius, thing->yPos() - radius, bottom);
			glVertex3f(thing->xPos() + radius, thing->yPos() - radius, bottom);
			glVertex3f(thing->xPos() + radius, thing->yPos() + radius, bottom);
			glVertex3f(thing->xPos() - radius, thing->yPos() + radius, bottom);
			glEnd();
			if (render_3d_things_style == 2)
			{
				// Top
				glBegin(GL_LINE_LOOP);
				glVertex3f(thing->xPos() - radius, thing->yPos() - radius, top);
				glVertex3f(thing->xPos() + radius, thing->yPos() - radius, top);
				glVertex3f(thing->xPos() + radius, thing->yPos() + radius, top);
				glVertex3f(thing->xPos() - radius, thing->yPos() + radius, top);
				glEnd();
				// Corners
				glBegin(GL_LINES);
				glVertex3f(thing->xPos() - radius, thing->yPos() - radius, bottom);
				glVertex3f(thing->xPos() - radius, thing->yPos() - radius, top);
				glVertex3f(thing->xPos() + radius, thing->yPos() - radius, bottom);
				glVertex3f(thing->xPos() + radius, thing->yPos() - radius, top);
				glVertex3f(thing->xPos() + radius, thing->yPos() + radius, bottom);
				glVertex3f(thing->xPos() + radius, thing->yPos() + radius, top);
				glVertex3f(thing->xPos() - radius, thing->yPos() + radius, bottom);
				glVertex3f(thing->xPos() - radius, thing->yPos() + radius, top);
				glEnd();
			}

			// Direction
			glPushMatrix();
			glTranslatef(thing->xPos(), thing->yPos(), bottom);
			glRotated(thing->getAngle(), 0, 0, 1);
			glBegin(GL_LINES);
			glVertex3f(0.0f, 0.0f, 0.0f);
			glVertex3f(radius, 0.0f, 0.0f);
			glVertex3f(radius, 0.0f, 0.0f);
			glVertex3f(radius - (radius*0.2f), -radius * 0.2f, 0.0f);
			glVertex3f(radius, 0.0f, 0.0f);
			glVertex3f(radius - (radius*0.2f), radius * 0.2f, 0.0f);
			glEnd();
			glPopMatrix();
		}

		glDepthMask(GL_TRUE);
		glEnable(GL_CULL_FACE);
	}
}

/* MapRenderer3D::renderThingSelection
 * Renders selection overlay for all selected things
 *******************************************************************/
void MapRenderer3D::renderThingSelection(vector<selection_3d_t>& selection, float alpha)
{
	// Do nothing if no things visible
	if (render_3d_things == 0 || !render_selection)
		return;

	// Setup gl stuff
	glLineWidth(2.0f);
	glDisable(GL_TEXTURE_2D);
	glDisable(GL_FOG);
	glDisable(GL_DEPTH_TEST);
	glEnable(GL_LINE_SMOOTH);

	// Setup colour
	rgba_t col1 = ColourConfiguration::getColour("map_3d_selection");
	col1.a *= alpha;
	OpenGL::setColour(col1);
	rgba_t col2 = col1;
	col2.a *= 0.5;

	// Go through selection
	double halfwidth, theight, x1, y1, x2, y2;
	for (unsigned a = 0; a < selection.size(); a++)
	{
		// Ignore if not a thing selection
		if (selection[a].type != MapEditor::SEL_THING)
			continue;

		// Get thing
		fpoint2_t strafe(cam_position.x+cam_strafe.x, cam_position.y+cam_strafe.y);
		MapThing* thing = map->getThing(selection[a].index);
		if (!thing)
			return;

		// Skip if not shown
		if (!things[selection[a].index].type->isDecoration() && render_3d_things == 2)
			continue;

		// Determine coordinates
		halfwidth = things[selection[a].index].sprite->getWidth() * 0.5;
		theight = things[selection[a].index].sprite->getHeight();
		if (things[selection[a].index].flags & ICON)
		{
			halfwidth = render_thing_icon_size*0.5;
			theight = render_thing_icon_size;
		}
		x1 = thing->xPos() - cam_strafe.x * halfwidth;
		y1 = thing->yPos() - cam_strafe.y * halfwidth;
		x2 = thing->xPos() + cam_strafe.x * halfwidth;
		y2 = thing->yPos() + cam_strafe.y * halfwidth;

		// Render outline
		double z = things[selection[a].index].z;
		OpenGL::setColour(col1, false);
		glBegin(GL_LINE_LOOP);
		glVertex3f(x1, y1, z + theight);
		glVertex3f(x1, y1, z);
		glVertex3f(x2, y2, z);
		glVertex3f(x2, y2, z + theight);
		glEnd();

		// Render fill
		OpenGL::setColour(col2, false);
		glBegin(GL_QUADS);
		glVertex3f(x1, y1, z + theight);
		glVertex3f(x1, y1, z);
		glVertex3f(x2, y2, z);
		glVertex3f(x2, y2, z + theight);
		glEnd();
	}
}

/* MapRenderer3D::updateFlatsVBO
 * (Re)builds the flats Vertex Buffer Object
 *******************************************************************/
void MapRenderer3D::updateFlatsVBO()
{
	if (!flats_use_vbo)
		return;

	// Create VBOs if needed
	if (vbo_flats == 0)
		glGenBuffers(1, &vbo_flats);

	// Get total size needed
	unsigned totalsize = 0;
	for (unsigned a = 0; a < map->nSectors(); a++)
	{
		// Slight chicken and egg problem: updateSector() has to run first to
		// create a flat_3d_t for each 3D floor, but it tries to update VBOs,
		// which may not exist the first time this function runs.
		// So, skip the VBOs specifically for this call.
		if (isSectorStale(a))
			updateSector(a, false);

		Polygon2D* poly = map->getSector(a)->getPolygon();
		totalsize += poly->vboDataSize() * sector_flats[a].size() * 2;
	}

	// Allocate buffer data
	glBindBuffer(GL_ARRAY_BUFFER, vbo_flats);
	Polygon2D::setupVBOPointers();
	glBufferData(GL_ARRAY_BUFFER, totalsize, NULL, GL_STATIC_DRAW);

	// Write polygon data to VBO
	unsigned offset = 0;
	unsigned index = 0;
	int height = 0;
	for (unsigned a = 0; a < sector_flats.size(); a++)
	{
		MapSector* sector = sector_flats[a][0].sector;
		Polygon2D* poly = sector->getPolygon();

		// TODO i realize we'll have to do this if any 3d floors are /added/, too
		for (unsigned b = 0; b < sector_flats[a].size(); b++)
		{
			// Write flat to VBO
			sector_flats[a][b].vbo_offset = offset;
			poly->setZ(sector_flats[a][b].plane);
			offset = poly->writeToVBO(offset);
		}

		// Reset polygon z
		poly->setZ(0.0f);
	}

	// Clean up
	glBindBuffer(GL_ARRAY_BUFFER, 0);
}

/* MapRenderer3D::updateWallsVBO
 * (Re)builds the walls Vertex Buffer Object (or would, if it were
 * used for wall rendering)
 *******************************************************************/
void MapRenderer3D::updateWallsVBO()
{
}

/* MapRenderer3D::quickVisDiscard
 * Runs a quick check of all sector bounding boxes against the
 * current view to hide any that are outside it
 *******************************************************************/
void MapRenderer3D::quickVisDiscard()
{
	// Create sector distance array if needed
	if (dist_sectors.size() != map->nSectors())
		dist_sectors.resize(map->nSectors());

	// Go through all sectors
	fpoint2_t cam = cam_position.get2d();
	double min_dist, dist;
	fseg2_t strafe(cam, cam + cam_strafe.get2d());
	for (unsigned a = 0; a < map->nSectors(); a++)
	{
		// Get sector bbox
		bbox_t bbox = map->getSector(a)->boundingBox();

		// Init to visible
		dist_sectors[a] = 0.0f;

		// Check if within bbox
		if (bbox.contains(cam))
			continue;

		// Check side of camera
		if (cam_pitch > -0.9 && cam_pitch < 0.9)
		{
			if (MathStuff::lineSide(bbox.min, strafe) > 0 &&
			        MathStuff::lineSide(fpoint2_t(bbox.max.x, bbox.min.y), strafe) > 0 &&
			        MathStuff::lineSide(bbox.max, strafe) > 0 &&
			        MathStuff::lineSide(fpoint2_t(bbox.min.x, bbox.max.y), strafe) > 0)
			{
				// Behind camera, invisible
				dist_sectors[a] = -1.0f;
				continue;
			}
		}

		// Check distance to bbox
		if (render_max_dist > 0)
		{
			min_dist = 9999999;
			dist = MathStuff::distanceToLine(cam, bbox.left_side());
			if (dist < min_dist) min_dist = dist;
			dist = MathStuff::distanceToLine(cam, bbox.top_side());
			if (dist < min_dist) min_dist = dist;
			dist = MathStuff::distanceToLine(cam, bbox.right_side());
			if (dist < min_dist) min_dist = dist;
			dist = MathStuff::distanceToLine(cam, bbox.bottom_side());
			if (dist < min_dist) min_dist = dist;

			dist_sectors[a] = dist;
		}
	}

	// Set all lines that are part of invisible sectors to invisible
	for (unsigned a = 0; a < map->nSides(); a++)
	{
		dist = dist_sectors[map->getSide(a)->getSector()->getIndex()];
		if (dist < 0 || (render_max_dist > 0 && dist > render_max_dist))
			lines[map->getSide(a)->getParentLine()->getIndex()].visible = false;
		else
			lines[map->getSide(a)->getParentLine()->getIndex()].visible = true;
	}
}

/* MapRenderer3D::calcDistFade
 * Calculates and returns the faded alpha value for [distance] from
 * the camera
 *******************************************************************/
float MapRenderer3D::calcDistFade(double distance, double max)
{
	if (max <= 0)
		return 1.0f;

	float faderange = max * 0.2f;
	if (distance > max - faderange)
		return 1.0f - ((distance - (max - faderange)) / faderange);
	else
		return 1.0f;
}

/* MapRenderer3D::checkVisibleQuads
 * Checks and hides any quads that are not currently in view
 *******************************************************************/
void MapRenderer3D::checkVisibleQuads()
{
	// Create quads array if empty
	if (!quads)
		quads = (quad_3d_t**)malloc(sizeof(quad_3d_t*) * map->nLines() * 4);

	// Go through lines
	MapLine* line;
	float distfade;
	n_quads = 0;
	unsigned updates = 0;
	bool update = false;
	fseg2_t strafe(cam_position.get2d(), (cam_position + cam_strafe).get2d());
	for (unsigned a = 0; a < lines.size(); a++)
	{
		line = map->getLine(a);

		// Skip if not visible
		if (!lines[a].visible)
			continue;

		// Check side of camera
		if (cam_pitch > -0.9 && cam_pitch < 0.9)
		{
			if (MathStuff::lineSide(line->point1(), strafe) > 0 &&
			        MathStuff::lineSide(line->point2(), strafe) > 0)
				continue;
		}

		// Check for distance fade
		if (render_max_dist > 0)
			distfade = calcDistFade(MathStuff::distanceToLine(cam_position.get2d(), line->seg()), render_max_dist);
		else
			distfade = 1.0f;

		// Update line if needed
		update = false;
		if (lines[a].updated_time < line->modifiedTime())	// Check line modified
			update = true;
		if (lines[a].line != line)
			update = true;
		if (!update && line->s1())
		{
			// Check front side/sector modified
			if (lines[a].updated_time < line->s1()->modifiedTime() ||
			        lines[a].updated_time < line->frontSector()->modifiedTime() ||
			        lines[a].updated_time < line->frontSector()->geometryUpdatedTime())
				update = true;
		}
		if (!update && line->s2())
		{
			// Check back side/sector modified
			if (lines[a].updated_time < line->s2()->modifiedTime() ||
			        lines[a].updated_time < line->backSector()->modifiedTime() ||
			        lines[a].updated_time < line->backSector()->geometryUpdatedTime())
				update = true;
		}
		if (update)
		{
			updateLine(a);
			//updates++;
			//if (updates > 500)
			//	break;
		}

		// Determine quads to be drawn
		quad_3d_t* quad;
		for (unsigned q = 0; q < lines[a].quads.size(); q++)
		{
			// Check we're on the right side of the quad
			quad = &(lines[a].quads[q]);
			if (MathStuff::lineSide(cam_position.get2d(), fseg2_t(quad->points[0].x, quad->points[0].y, quad->points[2].x, quad->points[2].y)) < 0)
				continue;

			quads[n_quads] = quad;
			quad->alpha = distfade;
			n_quads++;
		}
	}
}

/* MapRenderer3D::checkVisibleFlats
 * Checks and hides any flats that are not currently in view
 *******************************************************************/
void MapRenderer3D::checkVisibleFlats()
{
	// Create flats array if empty
	flats.clear();
	flats.reserve(map->nSectors() * 2);

	// Go through sectors
	MapSector* sector;
	float alpha;
	fpoint2_t cam = cam_position.get2d();
	for (unsigned a = 0; a < map->nSectors(); a++)
	{
		sector = map->getSector(a);

		// Skip if invisible
		if (dist_sectors[a] < 0)
			continue;

		// Check distance if needed
		if (render_max_dist > 0)
		{
			if (dist_sectors[a] > render_max_dist)
				continue;
			// Double-check distance
			dist_sectors[a] = sector->distanceTo(cam, render_max_dist);
			if (dist_sectors[a] > render_max_dist && !sector->boundingBox().contains(cam))
			{
				dist_sectors[a] = -1;
				continue;
			}
		}

		// Update sector info if needed
		if (isSectorStale(a))
			updateSector(a);

		// Set distance fade alpha
		if (render_max_dist > 0)
			alpha = calcDistFade(dist_sectors[a], render_max_dist);
		else
			alpha = 1.0f;

		// TODO this used to add all the floors, then all the ceilings, which would reduce the number of GL state calls -- can probably fix that

		// Add flats
		for (unsigned b = 0; b < sector_flats[a].size(); b++)
		{
			flat_3d_t& flat = sector_flats[a][b];
			flat.alpha = alpha;
			flats.push_back(&flat);

			// For two-sided flats, update which plane is currently visible
			if (flat.flags & DRAWBOTH)
			{
				if (cam_position.z < flat.plane.height_at(cam_position.x, cam_position.y))
					flat.flags |= CEIL;
				else
					flat.flags &= ~CEIL;
			}
		}
	}

	n_flats = flats.size();
}

/* MapRenderer3D::determineHilight
 * Finds the closest wall/flat/thing to the camera along the view
 * vector
 *******************************************************************/
selection_3d_t MapRenderer3D::determineHilight()
{
	// Init
	double min_dist = 9999999;
	selection_3d_t current;
	fseg2_t strafe(cam_position.get2d(), (cam_position + cam_strafe).get2d());

	// Check for required map structures
	if (!map || lines.size() != map->nLines() ||
	        sector_flats.size() != map->nSectors() ||
	        things.size() != map->nThings())
		return current;

	// Check lines
	double height, dist;
	quad_3d_t* quad;
	for (unsigned a = 0; a < map->nLines(); a++)
	{
		// Ignore if not visible
		if (!lines[a].visible)
			continue;

		MapLine* line = map->getLine(a);

		// Find (2d) distance to line
		dist = MathStuff::distanceRayLine(
			cam_position.get2d(), (cam_position + cam_dir3d).get2d(),
			line->point1(), line->point2());

		// Ignore if no intersection or something was closer
		if (dist < 0 || dist >= min_dist)
			continue;

		// Find quad intersect if any
		fpoint3_t intersection = cam_position + cam_dir3d * dist;
		for (unsigned q = 0; q < lines[a].quads.size(); q++)
		{
			quad = &lines[a].quads[q];

			// Check side of camera
			if (MathStuff::lineSide(cam_position.get2d(), fseg2_t(quad->points[0].x, quad->points[0].y, quad->points[2].x, quad->points[2].y)) < 0)
				continue;

			// Check intersection height
			// Need to handle slopes by finding the floor and ceiling height of
			// the quad at the intersection point
			fpoint2_t seg_left = fpoint2_t(quad->points[1].x, quad->points[1].y);
			fpoint2_t seg_right = fpoint2_t(quad->points[2].x, quad->points[2].y);
			double dist_along_segment =
				(intersection.get2d() - seg_left).magnitude() /
				(seg_right - seg_left).magnitude();
			double top = quad->points[0].z + (quad->points[3].z - quad->points[0].z) * dist_along_segment;
			double bottom = quad->points[1].z + (quad->points[2].z - quad->points[1].z) * dist_along_segment;
			if (bottom <= intersection.z && intersection.z <= top)
			{
				// Determine selected item from quad flags

				// Side index
				if (quad->flags & BACK)
					current.index = line->s2Index();
				else
					current.index = line->s1Index();

				// Side part
				if (quad->flags & UPPER)
					current.type = MapEditor::SEL_SIDE_TOP;
				else if (quad->flags & LOWER)
					current.type = MapEditor::SEL_SIDE_BOTTOM;
				else
					current.type = MapEditor::SEL_SIDE_MIDDLE;

				min_dist = dist;
			}
		}
	}

	// Check sectors
	for (unsigned a = 0; a < map->nSectors(); a++)
	{
		// Ignore if not visible
		if (dist_sectors[a] < 0)
			continue;

		// Check distance to planes
		for (unsigned b = 0; b < sector_flats[a].size(); b++)
		{
			flat_3d_t& flat = sector_flats[a][b];

			dist = MathStuff::distanceRayPlane(cam_position, cam_dir3d, sector_flats[a][b].plane);
			if (dist < 0 || dist >= min_dist)
				continue;

			// Check if on the correct side of the plane
			double flat_z = sector_flats[a][b].plane.height_at(cam_position.x, cam_position.y);
			if (flat.flags & CEIL && cam_position.z >= flat_z)
				continue;
			if (!(flat.flags & CEIL) && cam_position.z <= flat_z)
				continue;

			// Check if intersection is within sector
			if (!map->getSector(a)->isWithin((cam_position + cam_dir3d * dist).get2d()))
				continue;

			current.index = a;
			current.extra_floor_index = flat.extra_floor_index;
			min_dist = dist;
			if (flat.flags & CEIL)
				current.type = MapEditor::SEL_CEILING;
			else
				current.type = MapEditor::SEL_FLOOR;
		}
	}

	// Update item distance
	if (min_dist >= 9999999 || min_dist < 0)
		item_dist = -1;
	else
		item_dist = MathStuff::round(min_dist);

	// Check things (if visible)
	if (render_3d_things == 0)
		return current;
	double halfwidth, theight;
	for (unsigned a = 0; a < map->nThings(); a++)
	{
		// Ignore if no sprite
		if (!things[a].sprite)
			continue;

		// Ignore if not visible
		MapThing* thing = map->getThing(a);
		if (MathStuff::lineSide(thing->point(), strafe) > 0)
			continue;

		// Ignore if not shown
		if (!things[a].type->isDecoration() && render_3d_things == 2)
			continue;

		// Find distance to thing sprite
		halfwidth = things[a].sprite->getWidth() * 0.5;
		if (things[a].flags & ICON)
			halfwidth = render_thing_icon_size*0.5;
		dist = MathStuff::distanceRayLine(
			cam_position.get2d(), (cam_position + cam_dir3d).get2d(),
			thing->point() - cam_strafe.get2d() * halfwidth, thing->point() + cam_strafe.get2d() * halfwidth);

		// Ignore if no intersection or something was closer
		if (dist < 0 || dist >= min_dist)
			continue;

		// Check intersection height
		theight = things[a].sprite->getHeight();
		height = cam_position.z + cam_dir3d.z*dist;
		if (things[a].flags & ICON)
			theight = render_thing_icon_size;
		if (height >= things[a].z && height <= things[a].z + theight)
		{
			current.index = a;
			current.type = MapEditor::SEL_THING;
			min_dist = dist;
		}
	}

	// Update item distance
	if (min_dist >= 9999999 || min_dist < 0)
		item_dist = -1;
	else
		item_dist = MathStuff::round(min_dist);

	return current;
}

/* MapRenderer3D::renderHilight
 * Renders the hilight overlay for the currently hilighted object
 *******************************************************************/
void MapRenderer3D::renderHilight(selection_3d_t hilight, float alpha)
{
	// Do nothing if no item hilighted
	if (hilight.index < 0 || render_3d_hilight == 0 || !render_hilight)
		return;

	// Setup gl stuff
	glLineWidth(3.0f);
	glDisable(GL_TEXTURE_2D);
	glDisable(GL_FOG);
	glDisable(GL_DEPTH_TEST);
	glEnable(GL_LINE_SMOOTH);
	rgba_t col_hilight = ColourConfiguration::getColour("map_3d_hilight");
	col_hilight.a *= alpha;
	OpenGL::setColour(col_hilight);

	// Quad hilight
	if (hilight.type == MapEditor::SEL_SIDE_BOTTOM || hilight.type == MapEditor::SEL_SIDE_MIDDLE || hilight.type == MapEditor::SEL_SIDE_TOP)
	{
		// Get side
		MapSide* side = map->getSide(hilight.index);
		if (!side)
			return;

		// Get parent line index
		int line = map->getSide(hilight.index)->getParentLine()->getIndex();

		// Get appropriate quad
		quad_3d_t* quad = NULL;
		for (unsigned a = 0; a < lines[line].quads.size(); a++)
		{
			// Check quad is correct side
			if (map->getLine(line)->s1() == side && lines[line].quads[a].flags & BACK)
				continue;

			// Check quad is correct part
			if (lines[line].quads[a].flags & UPPER)
			{
				if (hilight.type == MapEditor::SEL_SIDE_TOP)
				{
					quad = &lines[line].quads[a];
					break;
				}
			}
			else if (lines[line].quads[a].flags & LOWER)
			{
				if (hilight.type == MapEditor::SEL_SIDE_BOTTOM)
				{
					quad = &lines[line].quads[a];
					break;
				}
			}
			else if (hilight.type == MapEditor::SEL_SIDE_MIDDLE)
			{
				quad = &lines[line].quads[a];
				break;
			}
		}

		if (!quad)
			return;

		// Render outline
		glBegin(GL_LINE_LOOP);
		for (unsigned a = 0; a < 4; a++)
			glVertex3f(quad->points[a].x, quad->points[a].y, quad->points[a].z);
		glEnd();

		// Render fill (if needed)
		if (render_3d_hilight > 1)
		{
			glCullFace(GL_BACK);
			col_hilight.a *= 0.3;
			OpenGL::setColour(col_hilight, false);
			glBegin(GL_QUADS);
			for (unsigned a = 0; a < 4; a++)
				glVertex3f(quad->points[a].x, quad->points[a].y, quad->points[a].z);
			glEnd();
		}
	}

	// Sector hilight
	if (hilight.type == MapEditor::SEL_FLOOR || hilight.type == MapEditor::SEL_CEILING)
	{
		// Get sector
		MapSector* sector = map->getSector(hilight.index);
		if (!sector)
			return;

		// Get plane
		plane_t plane;
		if (hilight.extra_floor_index >= 0 && hilight.extra_floor_index < sector->extra_floors.size())
		{
			extra_floor_t& extra = sector->extra_floors[hilight.extra_floor_index];
			MapSector* control_sector = map->getSector(extra.control_sector_index);
			if (!control_sector)
				return;

			// Planes are reversed for a 3D floor
			// TODO the DRAWBOTH hack makes the type wrong when you're inside
			// TODO not true for vavoom
			if (hilight.type == MapEditor::SEL_FLOOR)
				plane = control_sector->getCeilingPlane();
			else
				plane = control_sector->getFloorPlane();
		}
		else
		{
			if (hilight.type == MapEditor::SEL_FLOOR)
				plane = sector->getFloorPlane();
			else
				plane = sector->getCeilingPlane();
		}

		// Render sector outline
		vector<MapLine*> lines;
		sector->getLines(lines);
		glBegin(GL_LINES);
		for (unsigned a = 0; a < lines.size(); a++)
		{
			glVertex3d(lines[a]->x1(), lines[a]->y1(), plane.height_at(lines[a]->x1(), lines[a]->y1()));
			glVertex3d(lines[a]->x2(), lines[a]->y2(), plane.height_at(lines[a]->x2(), lines[a]->y2()));
		}
		glEnd();

		// Render fill if needed
		if (render_3d_hilight > 1)
		{
			col_hilight.a *= 0.3;
			OpenGL::setColour(col_hilight, false);
			glDisable(GL_CULL_FACE);
			sector->getPolygon()->setZ(plane);
			sector->getPolygon()->render();
			sector->getPolygon()->setZ(0);
			glEnable(GL_CULL_FACE);
		}
	}

	// Thing hilight
	double x1, y1, x2, y2;
	if (hilight.type == MapEditor::SEL_THING)
	{
		// Get thing
		fpoint2_t strafe(cam_position.x+cam_strafe.x, cam_position.y+cam_strafe.y);
		MapThing* thing = map->getThing(hilight.index);
		if (!thing)
			return;

		// Determine coordinates
		double halfwidth = things[hilight.index].type->getScaleX() * things[hilight.index].sprite->getWidth() * 0.5;
		double theight = things[hilight.index].type->getScaleY() * things[hilight.index].sprite->getHeight();
		if (things[hilight.index].flags & ICON)
		{
			halfwidth = render_thing_icon_size*0.5;
			theight = render_thing_icon_size;
		}
		x1 = thing->xPos() - cam_strafe.x * halfwidth;
		y1 = thing->yPos() - cam_strafe.y * halfwidth;
		x2 = thing->xPos() + cam_strafe.x * halfwidth;
		y2 = thing->yPos() + cam_strafe.y * halfwidth;

		// Render outline of sprite
		double z = things[hilight.index].z;
		glBegin(GL_LINE_LOOP);
		glVertex3f(x1, y1, z + theight);
		glVertex3f(x1, y1, z);
		glVertex3f(x2, y2, z);
		glVertex3f(x2, y2, z + theight);
		glEnd();

		// Render fill if needed
		if (render_3d_hilight > 1)
		{
			glCullFace(GL_BACK);
			col_hilight.a *= 0.3;
			OpenGL::setColour(col_hilight, false);
			glBegin(GL_QUADS);
			glVertex3f(x1, y1, z + theight);
			glVertex3f(x1, y1, z);
			glVertex3f(x2, y2, z);
			glVertex3f(x2, y2, z + theight);
			glEnd();
		}
	}

	//glEnable(GL_DEPTH_TEST);
	OpenGL::setColour(COL_WHITE);
}

/* MapRenderer3D::onAnnouncements
 * Handles any announcements from the palette or resource manager
 *******************************************************************/
void MapRenderer3D::onAnnouncement(Announcer* announcer, string event_name, MemChunk& event_data)
{
	if (announcer != thePaletteChooser && announcer != theResourceManager)
		return;

	if (event_name == "resources_updated" || event_name == "main_palette_changed")
	{
		// Refresh lines
		for (unsigned a = 0; a < lines.size(); a++)
		{
			for (unsigned q = 0; q < lines[a].quads.size(); q++)
				lines[a].quads[q].texture = NULL;

			lines[a].updated_time = 0;
		}

		// Refresh flats
		for (unsigned a = 0; a < sector_flats.size(); a++)
			for (unsigned b = 0; b < sector_flats[a].size(); b++)
			{
				sector_flats[a][b].texture = NULL;
				sector_flats[a][b].updated_time = 0;
			}

		// Refresh things
		for (unsigned a = 0; a < things.size(); a++)
		{
			things[a].sprite = NULL;
			things[a].updated_time = 0;
		}
	}
}
