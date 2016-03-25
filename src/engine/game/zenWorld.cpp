#include "ZenWorld.h"
#include "zenconvert/zenParser.h"
#include "utils/logger.h"
#include <string>
#include "zenconvert/vob.h"
#include "zenconvert/zCMesh.h"
#include "vdfs/fileIndex.h"

#include "zenconvert/zCProgMeshProto.h"
#include "zenconvert/zenParser.h"
#include "engine.h"

#ifdef ZE_GAME
#include "renderer/visualLoad.h"
#include <RBuffer.h>
#include <RStateMachine.h>
#include <RDevice.h>
#include <RTools.h>
#include "renderer/renderSystem.h"
#include "renderer/visuals/visual.h"
#endif

using namespace Engine;

ZenWorld::ZenWorld(::Engine::Engine& engine, const std::string & zenFile, VDFS::FileIndex & vdfs, float scale)
{
	m_pEngine = &engine;

	// Load zen from vdfs
	std::vector<uint8_t> data;
	vdfs.getFileData(zenFile, data);

	// Try to load from disk if this isn't in a vdf-archive
	if(data.empty())
	{
		ZenConvert::ZenParser parser = ZenConvert::ZenParser(zenFile);
		loadWorld(engine, parser, vdfs, scale);
	}
	else
	{
		// Load from memory
		ZenConvert::ZenParser parser = ZenConvert::ZenParser(data.data(), data.size());
		loadWorld(engine, parser, vdfs, scale);
	}
}

void ZenWorld::loadWorld(::Engine::Engine& engine, ZenConvert::ZenParser& parser, VDFS::FileIndex & vdfs, float scale)
{
	// Load a world
	ZenConvert::zCMesh* worldMesh = nullptr;
	ZenConvert::oCWorldData worldData;

	// Try to parse the zen-file
	try
	{
		parser.readHeader();
		worldData = parser.readWorld();
		worldMesh = parser.getWorldMesh();
	}
	catch(std::exception &e)
	{
		LogError() << "Failed to load ZEN-File. Reason: " << e.what();
		return;
	}

	if(worldMesh)
		disectWorldMesh(worldMesh, engine, vdfs, scale);

	parseWorldObjects(worldData, engine, vdfs, scale);

#ifdef ZE_GAME
	engine.renderSystemPtr()->getPagedVertexBuffer<Renderer::WorldVertex>().RebuildPages();
	engine.renderSystemPtr()->getPagedIndexBuffer<uint32_t>().RebuildPages();
#endif
}

ZenWorld::~ZenWorld()
{

}

/**
* @brief Disects the worldmesh into its parts and creates the needed entities
*/
void ZenWorld::disectWorldMesh(ZenConvert::zCMesh* mesh, ::Engine::Engine& engine, VDFS::FileIndex & vdfs, float scale)
{
	std::vector<ObjectHandle> handles;

	ZenConvert::PackedMesh packedMesh;

	// Pack the mesh into an easier format
	mesh->packMesh(packedMesh, scale);

	// Initialize world-mesh
	m_WorldMesh.setMeshData(packedMesh);

#ifdef ZE_GAME
	// Create the visual
	std::hash<std::string> hash;
	Renderer::Visual* pVisual = engine.renderSystemPtr()->createVisual(hash("__WORLDMESH"), packedMesh);	

	// Create entities for rendering	
	pVisual->createEntities(handles);
#endif

	// Create collisionmesh using the first entites collision-component
	// to get the triangles all lined up into one btTriangleMesh
	if(!handles.empty())
	{
		btTriangleMesh* wm = new btTriangleMesh;
		for(auto& t : packedMesh.triangles)
		{
			auto& v0 = t.vertices[0].Position;
			auto& v1 = t.vertices[1].Position;
			auto& v2 = t.vertices[2].Position;

			// Convert to btvector
			btVector3 v[] = { {v0.x, v0.y, v0.z}, {v1.x, v1.y, v1.z}, {v2.x, v2.y, v2.z} };
			wm->addTriangle(v[0], v[1], v[2]);
		}

		Components::Collision* pCc = engine.objectFactory().storage().addComponent<Components::Collision>(handles[0]);
		Physics::CollisionShape cShape(new btBvhTriangleMeshShape(wm, false));
		pCc->collisionType = Physics::ECollisionType::CT_WorldMesh;
		pCc->rigidBody.initPhysics(engine.physicsSystem(), cShape, Math::float3(0.0f, 0.0f, 0.0f));
		pCc->rigidBody.setRestitution(0.1f);
		pCc->rigidBody.setFriction(1.0f);
		cShape.shape()->setUserPointer(pCc);
	}

	// Force the worldmesh into the physics engine, don't wait for the thread
	engine.physicsSystem()->updateRigidBodies();

	// Create collisionmeshes for each material
	/*for(size_t s=0;s<handles.size();s++)
	{
		if(!packedMesh.subMeshes[s].indices.empty())
		{
			Components::Collision* pCc = engine.objectFactory().storage().addComponent<Components::Collision>(handles[s]);

			btTriangleMesh* wm = new btTriangleMesh;

			for(size_t i = 0; i < packedMesh.triangles.size(); i ++)
			{
				auto& v0 = packedMesh.triangles[i].Position;
				auto& v1 = packedMesh.[i + 1]].Position;
				auto& v2 = packedMesh.[i + 2]].Position;

				// Convert to btvector
				btVector3 v[] = { {v0.x, v0.y, v0.z}, {v1.x, v1.y, v1.z}, {v2.x, v2.y, v2.z} };
				wm->addTriangle(v[0], v[1], v[2]);
			}

			Physics::CollisionShape cShape(new btBvhTriangleMeshShape(wm, false), Physics::ECollisionType::CT_WorldMesh);
			pCc->rigidBody.initPhysics(engine.physicsSystem(), cShape, Math::float3(0.0f, 0.0f, 0.0f));
			pCc->rigidBody.setRestitution(0.1f);
			pCc->rigidBody.setFriction(1.0f);
		}
	}*/
} 

/**
* @brief Creates entities for the loaded oCWorld
*/
void ZenWorld::parseWorldObjects(const ZenConvert::oCWorldData& data, ::Engine::Engine& engine, VDFS::FileIndex & vdfs, float scale)
{
	std::function<void(const std::vector<ZenConvert::zCVobData>&)> fn = [&](const std::vector<ZenConvert::zCVobData>& vobs)
	{
		for(auto& v : vobs)
		{
			// Same for child-vobs
			fn(v.childVobs);

#ifdef ZE_GAME
			// TODO: Put this into an other function
			if(v.visual.find(".3DS") != std::string::npos && v.visual.find(".3DS") != 0) // TODO: Don't find twice
			{
				// Get full world-matrix
				Math::Matrix worldMatrix = v.rotationMatrix3x3.toMatrix(v.position * scale);
				Math::float4 f4Lighting = Math::float4(1.0f, 1.0f, 1.0f ,1.0f);
				
				// Calculate center of bbox as the startingpoint for tracing the ground-poly
				Math::float3 gpRayStart = (v.bbox[0] * 0.5f + v.bbox[1] * 0.5f) * scale;

				// Get ground polygon and interpolate lighting
				Physics::RayTestResult r = engine.physicsSystem()->rayTest(gpRayStart, gpRayStart + Math::float3(0,-2000,0), Physics::CT_WorldMesh);
				if(r.hitTriangleIndex == UINT_MAX)
				{
					// Try one more time from further above
					// FIXME: There are still some vobs not getting their groundpoly...
					gpRayStart.y = v.bbox[1].y;
					r = engine.physicsSystem()->rayTest(gpRayStart, gpRayStart + Math::float3(0,-2000,0), Physics::CT_WorldMesh);
				}

				if(r.hitTriangleIndex != UINT_MAX)
				{
					f4Lighting = m_WorldMesh.getTriangleList()[r.hitTriangleIndex].interpolateLighting(r.hitPosition);
				}
				else
				{
					// Didn't hit ANYTHING. Just give them some static value.
					const float fallbackLighting = 0.5f;
					f4Lighting = Math::float4(fallbackLighting,fallbackLighting,fallbackLighting, 1.0f);
				}

				std::hash<std::string> hash;
				size_t visualHash = hash(v.visual);
				Renderer::Visual* pVisual = engine.renderSystemPtr()->getVisualByHash(visualHash);

				if(!pVisual)
				{
					// Strip .3DS-Part
					std::string vname = v.visual.substr(0, v.visual.find(".3DS"));
					// Try to find the mesh of this
					if(!vdfs.getFileByName(vname + ".MRM", nullptr)) // Try progmesh-proto
					{
						continue; // Skip this vob
					}

					// Found it, load the mesh-information
					ZenConvert::zCProgMeshProto mesh(vname + ".MRM", vdfs);

					// Create some entities using this visual
					ZenConvert::PackedMesh packedMesh;

					// Pack the mesh into an easier format
					mesh.packMesh(packedMesh, scale);

					// Create the visual
					pVisual = engine.renderSystemPtr()->createVisual(hash(v.visual), packedMesh);								
				}

				// Create entities and init visuals
				std::vector<ObjectHandle> handles;
				pVisual->createEntities(handles);

				Math::Matrix m = Math::Matrix::CreateIdentity();

				ZenConvert::VobObjectInfo ocb;
				ocb.worldMatrix = worldMatrix;
				ocb.color = f4Lighting;
	
				// Create entities for each material
				RAPI::RBuffer* pObjectBuffer = nullptr;
				for(auto& h : handles)
				{
					Entity* entity = engine.objectFactory().storage().getEntity(h);
					Components::Visual* pVisComp = engine.objectFactory().storage().getComponent<Components::Visual>(h);
					if(!pObjectBuffer)
					{					
						pObjectBuffer = pVisComp->pObjectBuffer;
						//pObjectBuffer->UpdateData(&ocb);
					}

					pVisComp->colorMod = f4Lighting;

					entity->setWorldTransform(worldMatrix);
				}
				
#endif
			}
		}
	};

	fn(data.rootVobs);
}

void ZenWorld::render(const Math::Matrix& viewProj)
{
#ifdef ZE_GAME
	for(auto& p : m_VobPositions)
	{
		RAPI::RTools::LineRenderer.AddPointLocator(RAPI::RFloat3(reinterpret_cast<float*>(&p)), 1.0f, RAPI::RFloat4(1,0,0,1));
	}
#endif
}

/**
 * Creates the visual for the given file. Uses one from cache if it already exists
 */
Renderer::Visual * ZenWorld::loadVisual(const std::string & visual, float scale)
{
	std::hash<std::string> hash;
	size_t visualHash = hash(visual);
	Renderer::Visual* pVisual = m_pEngine->renderSystemPtr()->getVisualByHash(visualHash);

	if(!pVisual)
	{
		// Strip .3DS-Part
		std::string vname = visual.substr(0, visual.find(".3DS"));
		// Try to find the mesh of this
		if(!m_pEngine->vdfsFileIndex().getFileByName(vname + ".MRM", nullptr)) // Try progmesh-proto
		{
			return nullptr; // Not found
		}

		// Found it, load the mesh-information
		ZenConvert::zCProgMeshProto mesh(vname + ".MRM", m_pEngine->vdfsFileIndex());

		// Create some entities using this visual
		ZenConvert::PackedMesh packedMesh;

		// Pack the mesh into an easier format
		mesh.packMesh(packedMesh, scale);

		// Create the visual
		pVisual = m_pEngine->renderSystemPtr()->createVisual(hash(visual), packedMesh);								
	}

	return pVisual;
}
