//
// Copyright (c) 2009-2010 Mikko Mononen memon@inside.org
//
// This software is provided 'as-is', without any express or implied
// warranty.  In no event will the authors be held liable for any damages
// arising from the use of this software.
// Permission is granted to anyone to use this software for any purpose,
// including commercial applications, and to alter it and redistribute it
// freely, subject to the following restrictions:
// 1. The origin of this software must not be misrepresented; you must not
//    claim that you wrote the original software. If you use this software
//    in a product, an acknowledgment in the product documentation would be
//    appreciated but is not required.
// 2. Altered source versions must be plainly marked as such, and must not be
//    misrepresented as being the original software.
// 3. This notice may not be removed or altered from any source distribution.
//

#include "Sample_TileMesh.h"
#include "Interface.h"
#include "InputGeom.h"
#include "Sample.h"
#include "Recast.h"
#include "RecastDebugDraw.h"
#include "DetourNavMesh.h"
#include "DetourNavMeshBuilder.h"
#include "DetourDebugDraw.h"
#include "NavMeshTesterTool.h"
#include "NavMeshPruneTool.h"
#include "OffMeshConnectionTool.h"
#include "ConvexVolumeTool.h"
#include "CrowdTool.h"

#include "SDL.h"
#include "SDL_opengl.h"
#include "imgui.h"
#include <gl/GLU.h>

#include <ppl.h>
#include <agents.h>
#include <mutex>

#include <math.h>
#include <stdio.h>
#include <string.h>

#ifdef WIN32
#	define snprintf _snprintf
#endif


inline unsigned int nextPow2(unsigned int v)
{
	v--;
	v |= v >> 1;
	v |= v >> 2;
	v |= v >> 4;
	v |= v >> 8;
	v |= v >> 16;
	v++;
	return v;
}

inline unsigned int ilog2(unsigned int v)
{
	unsigned int r;
	unsigned int shift;
	r = (v > 0xffff) << 4; v >>= r;
	shift = (v > 0xff) << 3; v >>= shift; r |= shift;
	shift = (v > 0xf) << 2; v >>= shift; r |= shift;
	shift = (v > 0x3) << 1; v >>= shift; r |= shift;
	r |= (v >> 1);
	return r;
}

class NavMeshTileTool : public SampleTool
{
	Sample_TileMesh* m_sample;
	float m_hitPos[3];
	bool m_hitPosSet;
	
public:

	NavMeshTileTool() :
		m_sample(0),
		m_hitPosSet(false)
	{
		m_hitPos[0] = m_hitPos[1] = m_hitPos[2] = 0;
	}

	virtual ~NavMeshTileTool()
	{
	}

	virtual int type() { return TOOL_TILE_EDIT; }

	virtual void init(Sample* sample)
	{
		m_sample = (Sample_TileMesh*)sample; 
	}
	
	virtual void reset() {}

	virtual void handleMenu()
	{
#if 0
		imguiLabel("Create Tiles");
		if (imguiButton("Create All"))
		{
			if (m_sample)
				m_sample->buildAllTiles();
		}
#endif
		if (ImGui::Button("Remove All"))
		{
			if (m_sample)
				m_sample->removeAllTiles();
		}
	}

	virtual void handleClick(const float* /*s*/, const float* p, bool shift)
	{
		m_hitPosSet = true;
		rcVcopy(m_hitPos,p);
		if (m_sample)
		{
			if (shift)
				m_sample->removeTile(m_hitPos);
			else
				m_sample->buildTile(m_hitPos);
		}
	}

	virtual void handleToggle() {}

	virtual void handleStep() {}

	virtual void handleUpdate(const float /*dt*/) {}
	
	virtual void handleRender()
	{
		if (m_hitPosSet)
		{
			const float s = m_sample->getAgentRadius();
			glColor4ub(0,0,0,128);
			glLineWidth(2.0f);
			glBegin(GL_LINES);
			glVertex3f(m_hitPos[0]-s,m_hitPos[1]+0.1f,m_hitPos[2]);
			glVertex3f(m_hitPos[0]+s,m_hitPos[1]+0.1f,m_hitPos[2]);
			glVertex3f(m_hitPos[0],m_hitPos[1]-s+0.1f,m_hitPos[2]);
			glVertex3f(m_hitPos[0],m_hitPos[1]+s+0.1f,m_hitPos[2]);
			glVertex3f(m_hitPos[0],m_hitPos[1]+0.1f,m_hitPos[2]-s);
			glVertex3f(m_hitPos[0],m_hitPos[1]+0.1f,m_hitPos[2]+s);
			glEnd();
			glLineWidth(1.0f);
		}
	}
	
	virtual void handleRenderOverlay(double* proj, double* model, int* view)
	{
		GLdouble x, y, z;
		if (m_hitPosSet && gluProject((GLdouble)m_hitPos[0], (GLdouble)m_hitPos[1], (GLdouble)m_hitPos[2],
									  model, proj, view, &x, &y, &z))
		{
			int tx=0, ty=0;
			m_sample->getTilePos(m_hitPos, tx, ty);

			ImGui::RenderText((int)x + 5, -((int)y - 5), ImVec4(0, 0, 0, 220), "(%d,%d)", tx, ty);
		}
		
		// Tool help
		const int h = view[3];

		ImGui::RenderTextRight(-330, -(h - 40), ImVec4(255, 255, 255, 192),
			"LMB: Rebuild hit tile.  Shift+LMB: Clear hit tile.");
	}
};

Sample_TileMesh::Sample_TileMesh() :
	m_buildAll(true),
	m_totalBuildTimeMs(0),
	m_drawMode(DRAWMODE_NAVMESH),
	m_maxTiles(0),
	m_maxPolysPerTile(0),
	m_tileSize(128),
	m_tileCol(duRGBA(0,0,0,32)),
	m_tileBuildTime(0),
	m_tileMemUsage(0),
	m_tileTriCount(0)
{
	resetCommonSettings();
	memset(m_tileBmin, 0, sizeof(m_tileBmin));
	memset(m_tileBmax, 0, sizeof(m_tileBmax));
	
	m_outputPath = new char[MAX_PATH];
	setTool(new NavMeshTileTool);
}

Sample_TileMesh::~Sample_TileMesh()
{
	dtFreeNavMesh(m_navMesh);
	m_navMesh = 0;
	delete[] m_outputPath;
}

static const int NAVMESHSET_MAGIC = 'M'<<24 | 'S'<<16 | 'E'<<8 | 'T'; //'MSET';
static const int NAVMESHSET_VERSION = 2;

struct NavMeshSetHeader
{
	int magic;
	int version;
	int numTiles;
	dtNavMeshParams params;
};

struct NavMeshTileHeader
{
	dtTileRef tileRef;
	int dataSize;
};

void Sample_TileMesh::SaveMesh(const std::string& outputPath)
{
	saveAll(outputPath.c_str(), m_navMesh);
}

void Sample_TileMesh::saveAll(const char* path, const dtNavMesh* mesh)
{
	if (!mesh) return;
	
	FILE* fp = fopen(path, "wb");
	if (!fp)
		return;
	
	// Store header.
	NavMeshSetHeader header;
	header.magic = NAVMESHSET_MAGIC;
	header.version = NAVMESHSET_VERSION;
	header.numTiles = 0;
	for (int i = 0; i < mesh->getMaxTiles(); ++i)
	{
		const dtMeshTile* tile = mesh->getTile(i);
		if (!tile || !tile->header || !tile->dataSize) continue;
		header.numTiles++;
	}
	memcpy(&header.params, mesh->getParams(), sizeof(dtNavMeshParams));
	fwrite(&header, sizeof(NavMeshSetHeader), 1, fp);

	// Store tiles.
	for (int i = 0; i < mesh->getMaxTiles(); ++i)
	{
		const dtMeshTile* tile = mesh->getTile(i);
		if (!tile || !tile->header || !tile->dataSize) continue;

		NavMeshTileHeader tileHeader;
		tileHeader.tileRef = mesh->getTileRef(tile);
		tileHeader.dataSize = tile->dataSize;
		fwrite(&tileHeader, sizeof(tileHeader), 1, fp);

		fwrite(tile->data, tile->dataSize, 1, fp);
	}

	fclose(fp);
}

bool Sample_TileMesh::LoadMesh(const std::string& outputPath)
{
	dtNavMesh* mesh = loadAll(outputPath.c_str());
	if (mesh)
	{
		if (m_navMesh)
			dtFreeNavMesh(m_navMesh);
		m_navMesh = mesh;

		dtStatus status = m_navQuery->init(m_navMesh, MAX_NODES);
		if (dtStatusFailed(status))
		{
			m_ctx->log(RC_LOG_ERROR, "buildTiledNavigation: Could not init Detour navmesh query");
			return false;
		}

		if (m_tool)
			m_tool->init(this);
		initToolStates(this);

		return true;
	}

	return false;
}

dtNavMesh* Sample_TileMesh::loadAll(const char* path)
{
	FILE* fp = fopen(path, "rb");
	if (!fp) return 0;
	
	// Read header.
	NavMeshSetHeader header;
	size_t readLen = fread(&header, sizeof(NavMeshSetHeader), 1, fp);
	if (readLen != 1)
	{
		fclose(fp);
		return 0;
	}
	if (header.magic != NAVMESHSET_MAGIC)
	{
		fclose(fp);
		return 0;
	}
	if (header.version != NAVMESHSET_VERSION)
	{
		fclose(fp);
		return 0;
	}
	
	dtNavMesh* mesh = dtAllocNavMesh();
	if (!mesh)
	{
		fclose(fp);
		return 0;
	}
	dtStatus status = mesh->init(&header.params);
	if (dtStatusFailed(status))
	{
		fclose(fp);
		return 0;
	}
		
	// Read tiles.
	for (int i = 0; i < header.numTiles; ++i)
	{
		NavMeshTileHeader tileHeader;
		readLen = fread(&tileHeader, sizeof(tileHeader), 1, fp);
		if (readLen != 1)
			return 0;

		if (!tileHeader.tileRef || !tileHeader.dataSize)
			break;

		unsigned char* data = (unsigned char*)dtAlloc(tileHeader.dataSize, DT_ALLOC_PERM);
		if (!data) break;
		memset(data, 0, tileHeader.dataSize);
		readLen = fread(data, tileHeader.dataSize, 1, fp);
		if (readLen != 1)
			return 0;

		dtMeshHeader* header = (dtMeshHeader*)data;

		dtStatus status = mesh->addTile(data, tileHeader.dataSize, DT_TILE_FREE_DATA, tileHeader.tileRef, 0);

		m_ctx->log(RC_LOG_PROGRESS, "Read Tile: %d, %d (%d) = %d\n", header->x,
			header->y, header->layer, status);
	}
	
	fclose(fp);
	
	return mesh;
}

void Sample_TileMesh::ResetMesh()
{
	if (m_navMesh)
	{
		dtFreeNavMesh(m_navMesh);
		m_navMesh = 0;
	}
}

void Sample_TileMesh::getTileStatistics(int& width, int& height, int& maxTiles) const
{
	width = m_tilesWidth;
	height = m_tilesHeight;
	maxTiles = m_maxTiles;
}

void Sample_TileMesh::handleSettings()
{
	if (ImGui::CollapsingHeader("Mesh Generation", 0, true, true))
	{
		ImGui::Text("Tiling");
		ImGui::SliderFloat("TileSize", &m_tileSize, 16.0f, 1024.0f, "%.0f");

		if (m_geom)
		{
			const glm::vec3& bmin = m_geom->getMeshBoundsMin();
			const glm::vec3& bmax = m_geom->getMeshBoundsMax();
			int gw = 0, gh = 0;
			rcCalcGridSize(&bmin[0], &bmax[0], m_cellSize, &gw, &gh);
			const int ts = (int)m_tileSize;
			m_tilesWidth = (gw + ts - 1) / ts;
			m_tilesHeight = (gh + ts - 1) / ts;
			m_tilesCount = m_tilesWidth * m_tilesHeight;

			// Max tiles and max polys affect how the tile IDs are caculated.
			// There are 22 bits available for identifying a tile and a polygon.
			int tileBits = rcMin((int)ilog2(nextPow2(m_tilesCount)), 14);
			if (tileBits > 14) tileBits = 14;
			int polyBits = 22 - tileBits;
			m_maxTiles = 1 << tileBits;
			m_maxPolysPerTile = 1 << polyBits;
		}
		else
		{
			m_maxTiles = 0;
			m_maxPolysPerTile = 0;
			m_tilesWidth = 0;
			m_tilesHeight = 0;
			m_tilesCount = 0;
		}

		ImGui::Separator();
		Sample::handleCommonSettings();

		if (m_geom)
		{

			// custom bounding box controls
			ImGui::Text("Bounding Box");

			glm::vec3 min = m_geom->getMeshBoundsMin();
			glm::vec3 max = m_geom->getMeshBoundsMax();

			ImGui::SliderFloat("Min X", &min.x, m_geom->getRealMeshBoundsMin().x, m_geom->getRealMeshBoundsMax().x, "%.1f");
			ImGui::SliderFloat("Min Y", &min.y, m_geom->getRealMeshBoundsMin().y, m_geom->getRealMeshBoundsMax().y, "%.1f");
			ImGui::SliderFloat("Min Z", &min.z, m_geom->getRealMeshBoundsMin().z, m_geom->getRealMeshBoundsMax().z, "%.1f");
			ImGui::SliderFloat("Max X", &max.x, m_geom->getRealMeshBoundsMin().x, m_geom->getRealMeshBoundsMax().x, "%.1f");
			ImGui::SliderFloat("Max Y", &max.y, m_geom->getRealMeshBoundsMin().y, m_geom->getRealMeshBoundsMax().y, "%.1f");
			ImGui::SliderFloat("Max Z", &max.z, m_geom->getRealMeshBoundsMin().z, m_geom->getRealMeshBoundsMax().z, "%.1f");

			m_geom->setMeshBoundsMin(min);
			m_geom->setMeshBoundsMax(max);

			if (ImGui::Button("Reset Bounds"))
			{
				m_geom->resetMeshBounds();
			}
		}
	}
}

void Sample_TileMesh::handleTools()
{
	int type = !m_tool ? TOOL_NONE : m_tool->type();

	if (ImGui::RadioButton("Test Navmesh", type == TOOL_NAVMESH_TESTER))
	{
		setTool(new NavMeshTesterTool);
	}
	if (ImGui::RadioButton("Prune Navmesh", type == TOOL_NAVMESH_PRUNE))
	{
		setTool(new NavMeshPruneTool);
	}
	if (ImGui::RadioButton("Create Tiles", type == TOOL_TILE_EDIT))
	{
		setTool(new NavMeshTileTool);
	}
	if (ImGui::RadioButton("Create Off-Mesh Links", type == TOOL_OFFMESH_CONNECTION))
	{
		setTool(new OffMeshConnectionTool);
	}
	if (ImGui::RadioButton("Create Convex Volumes", type == TOOL_CONVEX_VOLUME))
	{
		setTool(new ConvexVolumeTool);
	}
	if (ImGui::RadioButton("Create Crowds", type == TOOL_CROWD))
	{
		setTool(new CrowdTool);
	}
	
	ImGui::Separator();

	ImGui::Indent();

	if (m_tool)
		m_tool->handleMenu();

	ImGui::Unindent();
}

void Sample_TileMesh::handleDebugMode()
{
	// Check which modes are valid.
	bool valid[MAX_DRAWMODE];
	for (int i = 0; i < MAX_DRAWMODE; ++i)
		valid[i] = false;
	
	if (m_geom)
	{
		valid[DRAWMODE_NAVMESH] = m_navMesh != 0;
		valid[DRAWMODE_NAVMESH_TRANS] = m_navMesh != 0;
		valid[DRAWMODE_NAVMESH_BVTREE] = m_navMesh != 0;
		valid[DRAWMODE_NAVMESH_NODES] = m_navQuery != 0;
		valid[DRAWMODE_NAVMESH_PORTALS] = m_navMesh != 0;
		valid[DRAWMODE_NAVMESH_INVIS] = m_navMesh != 0;
		valid[DRAWMODE_MESH] = true;
		valid[DRAWMODE_VOXELS] = false; // m_solid != 0;
		valid[DRAWMODE_VOXELS_WALKABLE] = false; // = m_solid != 0;
		valid[DRAWMODE_COMPACT] = false; // m_chf != 0;
		valid[DRAWMODE_COMPACT_DISTANCE] = false; // m_chf != 0;
		valid[DRAWMODE_COMPACT_REGIONS] = false; // m_chf != 0;
		valid[DRAWMODE_REGION_CONNECTIONS] = false; // m_cset != 0;
		valid[DRAWMODE_RAW_CONTOURS] = false; // m_cset != 0;
		valid[DRAWMODE_BOTH_CONTOURS] = false; // m_cset != 0;
		valid[DRAWMODE_CONTOURS] = false; // m_cset != 0;
		valid[DRAWMODE_POLYMESH] = false; // m_pmesh != 0;
		valid[DRAWMODE_POLYMESH_DETAIL] = false; // m_dmesh != 0;
	}
	
	int unavail = 0;
	for (int i = 0; i < MAX_DRAWMODE; ++i)
		if (!valid[i]) unavail++;
	
	if (unavail == MAX_DRAWMODE)
		return;
	
	ImGui::Text("Draw");

	if (valid[DRAWMODE_MESH] && ImGui::RadioButton("Input Mesh", m_drawMode == DRAWMODE_MESH))
		m_drawMode = DRAWMODE_MESH;
	if (valid[DRAWMODE_NAVMESH] && ImGui::RadioButton("Navmesh", m_drawMode == DRAWMODE_NAVMESH))
		m_drawMode = DRAWMODE_NAVMESH;
	if (valid[DRAWMODE_NAVMESH_INVIS] && ImGui::RadioButton("Navmesh Invis", m_drawMode == DRAWMODE_NAVMESH_INVIS))
		m_drawMode = DRAWMODE_NAVMESH_INVIS;
	if (valid[DRAWMODE_NAVMESH_TRANS] && ImGui::RadioButton("Navmesh Trans", m_drawMode == DRAWMODE_NAVMESH_TRANS))
		m_drawMode = DRAWMODE_NAVMESH_TRANS;
	if (valid[DRAWMODE_NAVMESH_BVTREE] && ImGui::RadioButton("Navmesh BVTree", m_drawMode == DRAWMODE_NAVMESH_BVTREE))
		m_drawMode = DRAWMODE_NAVMESH_BVTREE;
	if (valid[DRAWMODE_NAVMESH_NODES] && ImGui::RadioButton("Navmesh Nodes", m_drawMode == DRAWMODE_NAVMESH_NODES))
		m_drawMode = DRAWMODE_NAVMESH_NODES;
	if (valid[DRAWMODE_NAVMESH_PORTALS] && ImGui::RadioButton("Navmesh Portals", m_drawMode == DRAWMODE_NAVMESH_PORTALS))
		m_drawMode = DRAWMODE_NAVMESH_PORTALS;
	if (valid[DRAWMODE_VOXELS] && ImGui::RadioButton("Voxels", m_drawMode == DRAWMODE_VOXELS))
		m_drawMode = DRAWMODE_VOXELS;
	if (valid[DRAWMODE_VOXELS_WALKABLE] && ImGui::RadioButton("Walkable Voxels", m_drawMode == DRAWMODE_VOXELS_WALKABLE))
		m_drawMode = DRAWMODE_VOXELS_WALKABLE;
	if (valid[DRAWMODE_COMPACT] && ImGui::RadioButton("Compact", m_drawMode == DRAWMODE_COMPACT))
		m_drawMode = DRAWMODE_COMPACT;
	if (valid[DRAWMODE_COMPACT_DISTANCE] && ImGui::RadioButton("Compact Distance", m_drawMode == DRAWMODE_COMPACT_DISTANCE))
		m_drawMode = DRAWMODE_COMPACT_DISTANCE;
	if (valid[DRAWMODE_COMPACT_REGIONS] && ImGui::RadioButton("Compact Regions", m_drawMode == DRAWMODE_COMPACT_REGIONS))
		m_drawMode = DRAWMODE_COMPACT_REGIONS;
	if (valid[DRAWMODE_REGION_CONNECTIONS] && ImGui::RadioButton("Region Connections", m_drawMode == DRAWMODE_REGION_CONNECTIONS))
		m_drawMode = DRAWMODE_REGION_CONNECTIONS;
	if (valid[DRAWMODE_RAW_CONTOURS] && ImGui::RadioButton("Raw Contours", m_drawMode == DRAWMODE_RAW_CONTOURS))
		m_drawMode = DRAWMODE_RAW_CONTOURS;
	if (valid[DRAWMODE_BOTH_CONTOURS] && ImGui::RadioButton("Both Contours", m_drawMode == DRAWMODE_BOTH_CONTOURS))
		m_drawMode = DRAWMODE_BOTH_CONTOURS;
	if (valid[DRAWMODE_CONTOURS] && ImGui::RadioButton("Contours", m_drawMode == DRAWMODE_CONTOURS))
		m_drawMode = DRAWMODE_CONTOURS;
	if (valid[DRAWMODE_POLYMESH] && ImGui::RadioButton("Poly Mesh", m_drawMode == DRAWMODE_POLYMESH))
		m_drawMode = DRAWMODE_POLYMESH;
	if (valid[DRAWMODE_POLYMESH_DETAIL] && ImGui::RadioButton("Poly Mesh Detail", m_drawMode == DRAWMODE_POLYMESH_DETAIL))
		m_drawMode = DRAWMODE_POLYMESH_DETAIL;
		
	//if (unavail)
	//{
	//	imguiValue("Tick 'Keep Itermediate Results'");
	//	imguiValue("rebuild some tiles to see");
	//	imguiValue("more debug mode options.");
	//}
}

void Sample_TileMesh::handleRender()
{
	if (!m_geom || !m_geom->getMeshLoader())
		return;
	
	DebugDrawGL dd;

	const float texScale = 1.0f / (m_cellSize * 10.0f);
	
	// Draw mesh
	if (m_drawMode != DRAWMODE_NAVMESH_TRANS)
	{
		// Draw mesh
		duDebugDrawTriMeshSlope(&dd, m_geom->getMeshLoader()->getVerts(), m_geom->getMeshLoader()->getVertCount(),
								m_geom->getMeshLoader()->getTris(), m_geom->getMeshLoader()->getNormals(), m_geom->getMeshLoader()->getTriCount(),
								m_agentMaxSlope, texScale);
		m_geom->drawOffMeshConnections(&dd);
	}
		
	glDepthMask(GL_FALSE);
	
	// Draw bounds
	const glm::vec3& bmin = m_geom->getMeshBoundsMin();
	const glm::vec3& bmax = m_geom->getMeshBoundsMax();
	duDebugDrawBoxWire(&dd, bmin[0],bmin[1],bmin[2], bmax[0],bmax[1],bmax[2], duRGBA(255,255,255,128), 1.0f);
	
	// Tiling grid.
	int gw = 0, gh = 0;
	rcCalcGridSize(&bmin[0], &bmax[0], m_cellSize, &gw, &gh);
	const int tw = (gw + (int)m_tileSize-1) / (int)m_tileSize;
	const int th = (gh + (int)m_tileSize-1) / (int)m_tileSize;
	const float s = m_tileSize*m_cellSize;
	duDebugDrawGridXZ(&dd, bmin[0],bmin[1],bmin[2], tw,th, s, duRGBA(0,0,0,64), 1.0f);
	
	// Draw active tile
	duDebugDrawBoxWire(&dd, m_tileBmin[0],m_tileBmin[1],m_tileBmin[2],
					   m_tileBmax[0],m_tileBmax[1],m_tileBmax[2], m_tileCol, 1.0f);
		
	if (m_navMesh && m_navQuery &&
		(m_drawMode == DRAWMODE_NAVMESH ||
		 m_drawMode == DRAWMODE_NAVMESH_TRANS ||
		 m_drawMode == DRAWMODE_NAVMESH_BVTREE ||
		 m_drawMode == DRAWMODE_NAVMESH_NODES ||
		 m_drawMode == DRAWMODE_NAVMESH_PORTALS ||
		 m_drawMode == DRAWMODE_NAVMESH_INVIS))
	{
		if (m_drawMode != DRAWMODE_NAVMESH_INVIS)
			duDebugDrawNavMeshWithClosedList(&dd, *m_navMesh, *m_navQuery, m_navMeshDrawFlags);
		if (m_drawMode == DRAWMODE_NAVMESH_BVTREE)
			duDebugDrawNavMeshBVTree(&dd, *m_navMesh);
		if (m_drawMode == DRAWMODE_NAVMESH_PORTALS)
			duDebugDrawNavMeshPortals(&dd, *m_navMesh);
		if (m_drawMode == DRAWMODE_NAVMESH_NODES)
			duDebugDrawNavMeshNodes(&dd, *m_navQuery);
		duDebugDrawNavMeshPolysWithFlags(&dd, *m_navMesh, SAMPLE_POLYFLAGS_DISABLED, duRGBA(0,0,0,128));
	}
	
	
	glDepthMask(GL_TRUE);
	
	//if (m_chf && m_drawMode == DRAWMODE_COMPACT)
	//	duDebugDrawCompactHeightfieldSolid(&dd, *m_chf);
	//if (m_chf && m_drawMode == DRAWMODE_COMPACT_DISTANCE)
	//	duDebugDrawCompactHeightfieldDistance(&dd, *m_chf);
	//if (m_chf && m_drawMode == DRAWMODE_COMPACT_REGIONS)
	//	duDebugDrawCompactHeightfieldRegions(&dd, *m_chf);
	//if (m_solid && m_drawMode == DRAWMODE_VOXELS)
	//{
	//	glEnable(GL_FOG);
	//	duDebugDrawHeightfieldSolid(&dd, *m_solid);
	//	glDisable(GL_FOG);
	//}
	//if (m_solid && m_drawMode == DRAWMODE_VOXELS_WALKABLE)
	//{
	//	glEnable(GL_FOG);
	//	duDebugDrawHeightfieldWalkable(&dd, *m_solid);
	//	glDisable(GL_FOG);
	//}
	//if (m_cset && m_drawMode == DRAWMODE_RAW_CONTOURS)
	//{
	//	glDepthMask(GL_FALSE);
	//	duDebugDrawRawContours(&dd, *m_cset);
	//	glDepthMask(GL_TRUE);
	//}	
	//if (m_cset && m_drawMode == DRAWMODE_BOTH_CONTOURS)
	//{
	//	glDepthMask(GL_FALSE);
	//	duDebugDrawRawContours(&dd, *m_cset, 0.5f);
	//	duDebugDrawContours(&dd, *m_cset);
	//	glDepthMask(GL_TRUE);
	//}
	//if (m_cset && m_drawMode == DRAWMODE_CONTOURS)
	//{
	//	glDepthMask(GL_FALSE);
	//	duDebugDrawContours(&dd, *m_cset);
	//	glDepthMask(GL_TRUE);
	//}
	//if (m_chf && m_cset && m_drawMode == DRAWMODE_REGION_CONNECTIONS)
	//{
	//	duDebugDrawCompactHeightfieldRegions(&dd, *m_chf);
	//	
	//	glDepthMask(GL_FALSE);
	//	duDebugDrawRegionConnections(&dd, *m_cset);
	//	glDepthMask(GL_TRUE);
	//}
	//if (m_pmesh && m_drawMode == DRAWMODE_POLYMESH)
	//{
	//	glDepthMask(GL_FALSE);
	//	duDebugDrawPolyMesh(&dd, *m_pmesh);
	//	glDepthMask(GL_TRUE);
	//}
	//if (m_dmesh && m_drawMode == DRAWMODE_POLYMESH_DETAIL)
	//{
	//	glDepthMask(GL_FALSE);
	//	duDebugDrawPolyMeshDetail(&dd, *m_dmesh);
	//	glDepthMask(GL_TRUE);
	//}
		
	m_geom->drawConvexVolumes(&dd);
	
	if (m_tool)
		m_tool->handleRender();
	renderToolStates();

	glDepthMask(GL_TRUE);
}

void Sample_TileMesh::handleRenderOverlay(double* proj, double* model, int* view)
{
	GLdouble x, y, z;
	
	// Draw start and end point labels
	if (m_tileBuildTime > 0.0f
		&& gluProject(
			(GLdouble)(m_tileBmin[0]+m_tileBmax[0])/2,
			(GLdouble)(m_tileBmin[1]+m_tileBmax[1])/2,
			(GLdouble)(m_tileBmin[2]+m_tileBmax[2])/2,
			model, proj, view, &x, &y, &z))
	{
		ImGui::RenderText((int)x, -((int)y - 25), ImVec4(0, 0, 0, 220),
			"%.3fms / %dTris / %.1fkB", m_tileBuildTime, m_tileTriCount, m_tileMemUsage);
	}
	
	if (m_tool)
		m_tool->handleRenderOverlay(proj, model, view);
	renderOverlayToolStates(proj, model, view);
}

void Sample_TileMesh::handleMeshChanged(class InputGeom* geom)
{
	Sample::handleMeshChanged(geom);

	dtFreeNavMesh(m_navMesh);
	m_navMesh = 0;

	if (m_tool)
	{
		m_tool->reset();
		m_tool->init(this);
	}
	resetToolStates();
	initToolStates(this);
}

bool Sample_TileMesh::handleBuild()
{
	if (!m_geom || !m_geom->getMeshLoader())
	{
		m_ctx->log(RC_LOG_ERROR, "buildTiledNavigation: No vertices and triangles.");
		return false;
	}
	
	dtFreeNavMesh(m_navMesh);
	
	m_navMesh = dtAllocNavMesh();
	if (!m_navMesh)
	{
		m_ctx->log(RC_LOG_ERROR, "buildTiledNavigation: Could not allocate navmesh.");
		return false;
	}

	dtNavMeshParams params;
	rcVcopy(params.orig, &m_geom->getMeshBoundsMin()[0]);
	params.tileWidth = m_tileSize*m_cellSize;
	params.tileHeight = m_tileSize*m_cellSize;
	params.maxTiles = m_maxTiles;
	params.maxPolys = m_maxPolysPerTile;
	
	dtStatus status;
	
	status = m_navMesh->init(&params);
	if (dtStatusFailed(status))
	{
		m_ctx->log(RC_LOG_ERROR, "buildTiledNavigation: Could not init navmesh.");
		return false;
	}
	
	status = m_navQuery->init(m_navMesh, MAX_NODES);
	if (dtStatusFailed(status))
	{
		m_ctx->log(RC_LOG_ERROR, "buildTiledNavigation: Could not init Detour navmesh query");
		return false;
	}

	if (m_buildAll)
		buildAllTiles();
	
	if (m_tool)
		m_tool->init(this);

	initToolStates(this);

	return true;
}

void Sample_TileMesh::buildTile(const float* pos)
{
	if (!m_geom) return;
	if (!m_navMesh) return;
		
	const glm::vec3& bmin = m_geom->getMeshBoundsMin();
	const glm::vec3& bmax = m_geom->getMeshBoundsMax();
	
	const float ts = m_tileSize*m_cellSize;
	const int tx = (int)((pos[0] - bmin[0]) / ts);
	const int ty = (int)((pos[2] - bmin[2]) / ts);
	
	m_tileBmin[0] = bmin[0] + tx*ts;
	m_tileBmin[1] = bmin[1];
	m_tileBmin[2] = bmin[2] + ty*ts;
	
	m_tileBmax[0] = bmin[0] + (tx+1)*ts;
	m_tileBmax[1] = bmax[1];
	m_tileBmax[2] = bmin[2] + (ty+1)*ts;
	
	m_tileCol = duRGBA(255,255,255,64);
	
	m_ctx->resetLog();
	
	int dataSize = 0;
	unsigned char* data = buildTileMesh(tx, ty, m_tileBmin, m_tileBmax, dataSize);

	// Remove any previous data (navmesh owns and deletes the data).
	m_navMesh->removeTile(m_navMesh->getTileRefAt(tx,ty,0),0,0);

	// Add tile, or leave the location empty.
	if (data)
	{
		// Let the navmesh own the data.
		dtStatus status = m_navMesh->addTile(data,dataSize,DT_TILE_FREE_DATA,0,0);
		if (dtStatusFailed(status))
			dtFree(data);
	}
	
	m_ctx->dumpLog("Build Tile (%d,%d):", tx,ty);
}

void Sample_TileMesh::getTilePos(const float* pos, int& tx, int& ty)
{
	if (!m_geom) return;
	
	const glm::vec3& bmin = m_geom->getMeshBoundsMin();
	
	const float ts = m_tileSize*m_cellSize;
	tx = (int)((pos[0] - bmin[0]) / ts);
	ty = (int)((pos[2] - bmin[2]) / ts);
}

void Sample_TileMesh::removeTile(const float* pos)
{
	if (!m_geom) return;
	if (!m_navMesh) return;
	
	const glm::vec3& bmin = m_geom->getMeshBoundsMin();
	const glm::vec3& bmax = m_geom->getMeshBoundsMax();

	const float ts = m_tileSize*m_cellSize;
	const int tx = (int)((pos[0] - bmin[0]) / ts);
	const int ty = (int)((pos[2] - bmin[2]) / ts);
	
	m_tileBmin[0] = bmin[0] + tx*ts;
	m_tileBmin[1] = bmin[1];
	m_tileBmin[2] = bmin[2] + ty*ts;
	
	m_tileBmax[0] = bmin[0] + (tx+1)*ts;
	m_tileBmax[1] = bmax[1];
	m_tileBmax[2] = bmin[2] + (ty+1)*ts;
	
	m_tileCol = duRGBA(128,32,16,64);
	
	m_navMesh->removeTile(m_navMesh->getTileRefAt(tx,ty,0),0,0);
}

struct TileData
{
	unsigned char* data = 0;
	int length = 0;
	int x = 0;
	int y = 0;
};

typedef std::shared_ptr<TileData> TileDataPtr;

class NavmeshUpdaterAgent : public concurrency::agent
{
public:
	explicit NavmeshUpdaterAgent(concurrency::ISource<TileDataPtr>& dataSource,
		dtNavMesh* navMesh)
		: m_source(dataSource)
		, m_navMesh(navMesh)
	{
	}

protected:
	virtual void run() override
	{
		TileDataPtr data = receive(m_source);
		while (data != nullptr)
		{
			// Remove any previous data (navmesh owns and deletes the data).
			m_navMesh->removeTile(m_navMesh->getTileRefAt(data->x, data->y, 0), 0, 0);

			// Let the navmesh own the data.
			dtStatus status = m_navMesh->addTile(data->data, data->length, DT_TILE_FREE_DATA, 0, 0);
			if (dtStatusFailed(status))
			{
				dtFree(data->data);
			}

			data = receive(m_source);
		}

		done();
	}

private:
	concurrency::ISource<TileDataPtr>& m_source;
	dtNavMesh* m_navMesh;
};

void Sample_TileMesh::buildAllTiles(bool async)
{
	if (!m_geom) return;
	if (!m_navMesh) return;
	if (m_buildingTiles) return;

	// if async, invoke on a new thread
	if (async)
	{
		if (m_buildThread.joinable())
			m_buildThread.join();

		m_buildThread = std::thread([this]()
		{
			buildAllTiles(false);
		});
		return;
	}

	m_buildingTiles = true;
	m_cancelTiles = false;
	
	const glm::vec3& bmin = m_geom->getMeshBoundsMin();
	const glm::vec3& bmax = m_geom->getMeshBoundsMax();
	int gw = 0, gh = 0;
	rcCalcGridSize(&bmin[0], &bmax[0], m_cellSize, &gw, &gh);
	const int ts = (int)m_tileSize;
	const int tw = (gw + ts-1) / ts;
	const int th = (gh + ts-1) / ts;
	const float tcs = m_tileSize*m_cellSize;

	m_tilesBuilt = 0;

	//concurrency::CurrentScheduler::Create(concurrency::SchedulerPolicy(1, concurrency::MaxConcurrency, 3));

	concurrency::unbounded_buffer<TileDataPtr> agentTiles;
	NavmeshUpdaterAgent updater_agent(agentTiles, m_navMesh);

	updater_agent.start();

	// Start the build process.
	m_ctx->startTimer(RC_TIMER_TEMP);

	concurrency::task_group tasks;
	for (int x = 0; x < tw; x++)
	{
		for (int y = 0; y < th; y++)
		{
			tasks.run([this, x, y, &bmin, &bmax, tcs, &agentTiles]()
			{
				if (m_cancelTiles)
					return;

				++m_tilesBuilt;

				m_tileBmin[0] = bmin[0] + x*tcs;
				m_tileBmin[1] = bmin[1];
				m_tileBmin[2] = bmin[2] + y*tcs;

				m_tileBmax[0] = bmin[0] + (x + 1)*tcs;
				m_tileBmax[1] = bmax[1];
				m_tileBmax[2] = bmin[2] + (y + 1)*tcs;

				int dataSize = 0;
				unsigned char* data = buildTileMesh(x, y, m_tileBmin, m_tileBmax, dataSize);

				if (data)
				{
					TileDataPtr tileData = std::make_shared<TileData>();
					tileData->data = data;
					tileData->length = dataSize;
					tileData->x = x;
					tileData->y = y;

					asend(agentTiles, tileData);
				}
			});
		}
	}

	tasks.wait();

	// send terminator
	asend(agentTiles, TileDataPtr());

	concurrency::agent::wait(&updater_agent);


	// Start the build process.
	m_ctx->stopTimer(RC_TIMER_TEMP);

	m_totalBuildTimeMs = m_ctx->getAccumulatedTime(RC_TIMER_TEMP)/1000.0f;

	m_buildingTiles = false;
}

void Sample_TileMesh::removeAllTiles()
{
	const glm::vec3& bmin = m_geom->getMeshBoundsMin();
	const glm::vec3& bmax = m_geom->getMeshBoundsMax();
	int gw = 0, gh = 0;
	rcCalcGridSize(&bmin[0], &bmax[0], m_cellSize, &gw, &gh);
	const int ts = (int)m_tileSize;
	const int tw = (gw + ts-1) / ts;
	const int th = (gh + ts-1) / ts;
	
	for (int y = 0; y < th; ++y)
	{
		for (int x = 0; x < tw; ++x)
			m_navMesh->removeTile(m_navMesh->getTileRefAt(x, y, 0), 0, 0);
	}
}

void Sample_TileMesh::cancelBuildAllTiles(bool wait)
{
	if (m_buildingTiles)
		m_cancelTiles = true;

	if (wait && m_buildThread.joinable())
		m_buildThread.join();
}

deleted_unique_ptr<rcCompactHeightfield> Sample_TileMesh::rasterizeGeometry(rcConfig& cfg) const
{
	// Allocate voxel heightfield where we rasterize our input data to.
	deleted_unique_ptr<rcHeightfield> solid(rcAllocHeightfield(),
		[](rcHeightfield* hf) { rcFreeHeightField(hf); });

	if (!rcCreateHeightfield(m_ctx, *solid, cfg.width, cfg.height, cfg.bmin, cfg.bmax, cfg.cs, cfg.ch))
	{
		m_ctx->log(RC_LOG_ERROR, "buildNavigation: Could not create solid heightfield.");
		return 0;
	}

	const float* verts = m_geom->getMeshLoader()->getVerts();
	const int nverts = m_geom->getMeshLoader()->getVertCount();
	const int ntris = m_geom->getMeshLoader()->getTriCount();
	const rcChunkyTriMesh* chunkyMesh = m_geom->getChunkyMesh();

	// Allocate array that can hold triangle flags.
	// If you have multiple meshes you need to process, allocate
	// and array which can hold the max number of triangles you need to process.

	std::unique_ptr<unsigned char[]> triareas(new unsigned char[chunkyMesh->maxTrisPerChunk]);

	float tbmin[2], tbmax[2];
	tbmin[0] = cfg.bmin[0];
	tbmin[1] = cfg.bmin[2];
	tbmax[0] = cfg.bmax[0];
	tbmax[1] = cfg.bmax[2];
	int cid[512];// TODO: Make grow when returning too many items.
	const int ncid = rcGetChunksOverlappingRect(chunkyMesh, tbmin, tbmax, cid, 512);
	if (!ncid)
		return 0;

	//m_tileTriCount = 0;

	for (int i = 0; i < ncid; ++i)
	{
		const rcChunkyTriMeshNode& node = chunkyMesh->nodes[cid[i]];
		const int* ctris = &chunkyMesh->tris[node.i * 3];
		const int nctris = node.n;

		//m_tileTriCount += nctris;

		memset(triareas.get(), 0, nctris*sizeof(unsigned char));
		rcMarkWalkableTriangles(m_ctx, cfg.walkableSlopeAngle,
			verts, nverts, ctris, nctris, triareas.get());

		rcRasterizeTriangles(m_ctx, verts, nverts, ctris, triareas.get(), nctris, *solid, cfg.walkableClimb);
	}

	// Once all geometry is rasterized, we do initial pass of filtering to
	// remove unwanted overhangs caused by the conservative rasterization
	// as well as filter spans where the character cannot possibly stand.
	rcFilterLowHangingWalkableObstacles(m_ctx, cfg.walkableClimb, *solid);
	rcFilterLedgeSpans(m_ctx, cfg.walkableHeight, cfg.walkableClimb, *solid);
	rcFilterWalkableLowHeightSpans(m_ctx, cfg.walkableHeight, *solid);

	// Compact the heightfield so that it is faster to handle from now on.
	// This will result more cache coherent data as well as the neighbours
	// between walkable cells will be calculated.
	deleted_unique_ptr<rcCompactHeightfield> chf(rcAllocCompactHeightfield(),
		[](rcCompactHeightfield* hf) { rcFreeCompactHeightfield(hf); });

	if (!rcBuildCompactHeightfield(m_ctx, cfg.walkableHeight, cfg.walkableClimb, *solid, *chf))
	{
		m_ctx->log(RC_LOG_ERROR, "buildNavigation: Could not build compact data.");
		return 0;
	}

	return std::move(chf);
}

unsigned char* Sample_TileMesh::buildTileMesh(const int tx, const int ty, const float* bmin, const float* bmax, int& dataSize) const
{
	if (!m_geom || !m_geom->getMeshLoader() || !m_geom->getChunkyMesh())
	{
		m_ctx->log(RC_LOG_ERROR, "buildNavigation: Input mesh is not specified.");
		return 0;
	}
	
	//m_tileMemUsage = 0;
	//m_tileBuildTime = 0;

	// Init build configuration from GUI
	rcConfig cfg;

	memset(&cfg, 0, sizeof(cfg));
	cfg.cs = m_cellSize;
	cfg.ch = m_cellHeight;
	cfg.walkableSlopeAngle = m_agentMaxSlope;
	cfg.walkableHeight = (int)ceilf(m_agentHeight / cfg.ch);
	cfg.walkableClimb = (int)floorf(m_agentMaxClimb / cfg.ch);
	cfg.walkableRadius = (int)ceilf(m_agentRadius / cfg.cs);
	cfg.maxEdgeLen = (int)(m_edgeMaxLen / m_cellSize);
	cfg.maxSimplificationError = m_edgeMaxError;
	cfg.minRegionArea = (int)rcSqr(m_regionMinSize);		// Note: area = size*size
	cfg.mergeRegionArea = (int)rcSqr(m_regionMergeSize);	// Note: area = size*size
	cfg.maxVertsPerPoly = (int)m_vertsPerPoly;
	cfg.tileSize = (int)m_tileSize;
	cfg.borderSize = cfg.walkableRadius + 3; // Reserve enough padding.
	cfg.width = cfg.tileSize + cfg.borderSize*2;
	cfg.height = cfg.tileSize + cfg.borderSize*2;
	cfg.detailSampleDist = m_detailSampleDist < 0.9f ? 0 : m_cellSize * m_detailSampleDist;
	cfg.detailSampleMaxError = m_cellHeight * m_detailSampleMaxError;
	
	// Expand the heighfield bounding box by border size to find the extents of geometry we need to build this tile.
	//
	// This is done in order to make sure that the navmesh tiles connect correctly at the borders,
	// and the obstacles close to the border work correctly with the dilation process.
	// No polygons (or contours) will be created on the border area.
	//
	// IMPORTANT!
	//
	//   :''''''''':
	//   : +-----+ :
	//   : |     | :
	//   : |     |<--- tile to build
	//   : |     | :  
	//   : +-----+ :<-- geometry needed
	//   :.........:
	//
	// You should use this bounding box to query your input geometry.
	//
	// For example if you build a navmesh for terrain, and want the navmesh tiles to match the terrain tile size
	// you will need to pass in data from neighbour terrain tiles too! In a simple case, just pass in all the 8 neighbours,
	// or use the bounding box below to only pass in a sliver of each of the 8 neighbours.
	rcVcopy(cfg.bmin, bmin);
	rcVcopy(cfg.bmax, bmax);
	cfg.bmin[0] -= cfg.borderSize*cfg.cs;
	cfg.bmin[2] -= cfg.borderSize*cfg.cs;
	cfg.bmax[0] += cfg.borderSize*cfg.cs;
	cfg.bmax[2] += cfg.borderSize*cfg.cs;
	
	// Reset build times gathering.
	m_ctx->resetTimers();
	
	// Start the build process.
	m_ctx->startTimer(RC_TIMER_TOTAL);
	
#if 0
	m_ctx->log(RC_LOG_PROGRESS, "Building navigation:");
	m_ctx->log(RC_LOG_PROGRESS, " - %d x %d cells", m_cfg.width, m_cfg.height);
	m_ctx->log(RC_LOG_PROGRESS, " - %.1fK verts, %.1fK tris", nverts/1000.0f, ntris/1000.0f);
#endif

	deleted_unique_ptr<rcCompactHeightfield> chf = rasterizeGeometry(cfg);
	if (!chf)
		return 0;

	// Erode the walkable area by agent radius.
	if (!rcErodeWalkableArea(m_ctx, cfg.walkableRadius, *chf))
	{
		m_ctx->log(RC_LOG_ERROR, "buildNavigation: Could not erode.");
		return 0;
	}

	// (Optional) Mark areas.
	const ConvexVolume* vols = m_geom->getConvexVolumes();
	for (int i  = 0; i < m_geom->getConvexVolumeCount(); ++i)
		rcMarkConvexPolyArea(m_ctx, &vols[i].verts[0][0], vols[i].nverts, vols[i].hmin, vols[i].hmax, (unsigned char)vols[i].area, *chf);
	
	
	// Partition the heightfield so that we can use simple algorithm later to triangulate the walkable areas.
	// There are 3 martitioning methods, each with some pros and cons:
	// 1) Watershed partitioning
	//   - the classic Recast partitioning
	//   - creates the nicest tessellation
	//   - usually slowest
	//   - partitions the heightfield into nice regions without holes or overlaps
	//   - the are some corner cases where this method creates produces holes and overlaps
	//      - holes may appear when a small obstacles is close to large open area (triangulation can handle this)
	//      - overlaps may occur if you have narrow spiral corridors (i.e stairs), this make triangulation to fail
	//   * generally the best choice if you precompute the nacmesh, use this if you have large open areas
	// 2) Monotone partioning
	//   - fastest
	//   - partitions the heightfield into regions without holes and overlaps (guaranteed)
	//   - creates long thin polygons, which sometimes causes paths with detours
	//   * use this if you want fast navmesh generation
	// 3) Layer partitoining
	//   - quite fast
	//   - partitions the heighfield into non-overlapping regions
	//   - relies on the triangulation code to cope with holes (thus slower than monotone partitioning)
	//   - produces better triangles than monotone partitioning
	//   - does not have the corner cases of watershed partitioning
	//   - can be slow and create a bit ugly tessellation (still better than monotone)
	//     if you have large open areas with small obstacles (not a problem if you use tiles)
	//   * good choice to use for tiled navmesh with medium and small sized tiles
	
	if (m_partitionType == SAMPLE_PARTITION_WATERSHED)
	{
		// Prepare for region partitioning, by calculating distance field along the walkable surface.
		if (!rcBuildDistanceField(m_ctx, *chf))
		{
			m_ctx->log(RC_LOG_ERROR, "buildNavigation: Could not build distance field.");
			return false;
		}
		
		// Partition the walkable surface into simple regions without holes.
		if (!rcBuildRegions(m_ctx, *chf, cfg.borderSize, cfg.minRegionArea, cfg.mergeRegionArea))
		{
			m_ctx->log(RC_LOG_ERROR, "buildNavigation: Could not build watershed regions.");
			return false;
		}
	}
	else if (m_partitionType == SAMPLE_PARTITION_MONOTONE)
	{
		// Partition the walkable surface into simple regions without holes.
		// Monotone partitioning does not need distancefield.
		if (!rcBuildRegionsMonotone(m_ctx, *chf, cfg.borderSize, cfg.minRegionArea, cfg.mergeRegionArea))
		{
			m_ctx->log(RC_LOG_ERROR, "buildNavigation: Could not build monotone regions.");
			return false;
		}
	}
	else // SAMPLE_PARTITION_LAYERS
	{
		// Partition the walkable surface into simple regions without holes.
		if (!rcBuildLayerRegions(m_ctx, *chf, cfg.borderSize, cfg.minRegionArea))
		{
			m_ctx->log(RC_LOG_ERROR, "buildNavigation: Could not build layer regions.");
			return false;
		}
	}
	 	
	// Create contours.
	deleted_unique_ptr<rcContourSet> cset(rcAllocContourSet(), [](rcContourSet* cs) { rcFreeContourSet(cs); });
	if (!rcBuildContours(m_ctx, *chf, cfg.maxSimplificationError, cfg.maxEdgeLen, *cset))
	{
		m_ctx->log(RC_LOG_ERROR, "buildNavigation: Could not create contours.");
		return 0;
	}
	
	if (cset->nconts == 0)
	{
		return 0;
	}
	
	// Build polygon navmesh from the contours.
	deleted_unique_ptr<rcPolyMesh> pmesh(rcAllocPolyMesh(), [](rcPolyMesh* pm) { rcFreePolyMesh(pm); });
	if (!rcBuildPolyMesh(m_ctx, *cset, cfg.maxVertsPerPoly, *pmesh))
	{
		m_ctx->log(RC_LOG_ERROR, "buildNavigation: Could not triangulate contours.");
		return 0;
	}
	
	// Build detail mesh.
	deleted_unique_ptr<rcPolyMeshDetail> dmesh(rcAllocPolyMeshDetail(), [](rcPolyMeshDetail* pm) { rcFreePolyMeshDetail(pm); });
	if (!rcBuildPolyMeshDetail(m_ctx, *pmesh, *chf,
							   cfg.detailSampleDist, cfg.detailSampleMaxError,
							   *dmesh))
	{
		m_ctx->log(RC_LOG_ERROR, "buildNavigation: Could build polymesh detail.");
		return 0;
	}
	
	chf.reset();
	cset.reset();
	
	unsigned char* navData = 0;
	int navDataSize = 0;
	if (cfg.maxVertsPerPoly <= DT_VERTS_PER_POLYGON)
	{
		if (pmesh->nverts >= 0xffff)
		{
			// The vertex indices are ushorts, and cannot point to more than 0xffff vertices.
			m_ctx->log(RC_LOG_ERROR, "Too many vertices per tile %d (max: %d).", pmesh->nverts, 0xffff);
			return 0;
		}
		
		// Update poly flags from areas.
		for (int i = 0; i < pmesh->npolys; ++i)
		{
			if (pmesh->areas[i] == RC_WALKABLE_AREA)
				pmesh->areas[i] = SAMPLE_POLYAREA_GROUND;
			
			if (pmesh->areas[i] == SAMPLE_POLYAREA_GROUND ||
				pmesh->areas[i] == SAMPLE_POLYAREA_GRASS ||
				pmesh->areas[i] == SAMPLE_POLYAREA_ROAD)
			{
				pmesh->flags[i] = SAMPLE_POLYFLAGS_WALK;
			}
			else if (pmesh->areas[i] == SAMPLE_POLYAREA_WATER)
			{
				pmesh->flags[i] = SAMPLE_POLYFLAGS_SWIM;
			}
			else if (pmesh->areas[i] == SAMPLE_POLYAREA_DOOR)
			{
				pmesh->flags[i] = SAMPLE_POLYFLAGS_WALK | SAMPLE_POLYFLAGS_DOOR;
			}
		}
		
		dtNavMeshCreateParams params;
		memset(&params, 0, sizeof(params));
		params.verts = pmesh->verts;
		params.vertCount = pmesh->nverts;
		params.polys = pmesh->polys;
		params.polyAreas = pmesh->areas;
		params.polyFlags = pmesh->flags;
		params.polyCount = pmesh->npolys;
		params.nvp = pmesh->nvp;
		params.detailMeshes = dmesh->meshes;
		params.detailVerts = dmesh->verts;
		params.detailVertsCount = dmesh->nverts;
		params.detailTris = dmesh->tris;
		params.detailTriCount = dmesh->ntris;
		params.offMeshConVerts = m_geom->getOffMeshConnectionVerts();
		params.offMeshConRad = m_geom->getOffMeshConnectionRads();
		params.offMeshConDir = m_geom->getOffMeshConnectionDirs();
		params.offMeshConAreas = m_geom->getOffMeshConnectionAreas();
		params.offMeshConFlags = m_geom->getOffMeshConnectionFlags();
		params.offMeshConUserID = m_geom->getOffMeshConnectionId();
		params.offMeshConCount = m_geom->getOffMeshConnectionCount();
		params.walkableHeight = m_agentHeight;
		params.walkableRadius = m_agentRadius;
		params.walkableClimb = m_agentMaxClimb;
		params.tileX = tx;
		params.tileY = ty;
		params.tileLayer = 0;
		rcVcopy(params.bmin, pmesh->bmin);
		rcVcopy(params.bmax, pmesh->bmax);
		params.cs = cfg.cs;
		params.ch = cfg.ch;
		params.buildBvTree = true;
		
		if (!dtCreateNavMeshData(&params, &navData, &navDataSize))
		{
			m_ctx->log(RC_LOG_ERROR, "Could not build Detour navmesh.");
			return 0;
		}		
	}
	//m_tileMemUsage = navDataSize/1024.0f;
	
	m_ctx->stopTimer(RC_TIMER_TOTAL);
	
	// Show performance stats.
	//duLogBuildTimes(*m_ctx, m_ctx->getAccumulatedTime(RC_TIMER_TOTAL));
	//m_ctx->log(RC_LOG_PROGRESS, ">> Polymesh: %d vertices  %d polygons", pmesh->nverts, pmesh->npolys);
	
	//m_tileBuildTime = m_ctx->getAccumulatedTime(RC_TIMER_TOTAL)/1000.0f;

	dataSize = navDataSize;
	return navData;
}

void Sample_TileMesh::setOutputPath(const char* output_path)
{
	strcpy(m_outputPath, output_path);
	char buffer[512];
	sprintf(buffer, "%s\\MQ2Nav", output_path);
	CreateDirectory(buffer, NULL);
}
