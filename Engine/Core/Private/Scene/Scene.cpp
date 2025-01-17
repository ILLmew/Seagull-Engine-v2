#include "StdAfx.h"
#include "Scene/Scene.h"

#include "Archive/IDAllocator.h"

#include "Platform/OS.h"
#include "Scene/Camera/FirstPersonCamera.h"
#include "Archive/TextureAssetArchive.h"
#include "Archive/MaterialAssetArchive.h"
#include "Profile/Profile.h"

namespace SG
{

	namespace // anonymous namespace
	{
		IDAllocator<UInt32> gObjectIdAllocator;
	}

	static void _OnMeshComponentAdded(const Scene::Entity& entity, MeshComponent& comp)
	{
		comp.objectId = gObjectIdAllocator.Allocate();
	}

	static void _OnMeshComponentRemoved(const Scene::Entity& entity, MeshComponent& comp)
	{
		gObjectIdAllocator.Restore(comp.objectId);
	}

	Scene::Scene()
	{
		mEntityManager.GetComponentHooker<MeshComponent>().HookOnAdded(_OnMeshComponentAdded);
		mEntityManager.GetComponentHooker<MeshComponent>().HookOnRemoved(_OnMeshComponentRemoved);

		mSkyboxEntity = mEntityManager.CreateEntity();
		mSkyboxEntity.AddComponent<TagComponent>("__skybox");
		auto& mesh = mSkyboxEntity.AddComponent<MeshComponent>();
		LoadMesh(EGennerateMeshType::eSkybox, mesh);

		mpRootNode = New(TreeNode);
		SG_ASSERT(mpRootNode);
	}

	Scene::~Scene()
	{
		ClearTreeNodes();
	}

	void Scene::OnSceneLoad()
	{
		SG_PROFILE_FUNCTION();

		const auto camPos = Vector3f(0.0f, 3.0f, 7.0f);

		mpCameraEntity = CreateEntity("main_camera", camPos, Vector3f(1.0f), Vector3f(0.0f));
		auto& cam = mpCameraEntity->AddComponent<CameraComponent>();
		cam.pCamera = MakeRef<FirstPersonCamera>(camPos);
		cam.pCamera->SetPerspective(60.0f, OperatingSystem::GetMainWindow()->GetAspectRatio());
		cam.pCamera->SetMoveSpeed(5.0f);

		auto* pEntity = CreateEntity("directional_light_0", Vector3f{ 0.0f, 150.0f, 0.35f }, Vector3f(1.0f), Vector3f(0.0f, 0.0f, 0.0f));
		pEntity->AddTag<LightTag>();
		pEntity->AddComponent<DirectionalLightComponent>();

		pEntity = CreateEntity("point_light_0", { 1.25f, 0.75f, -0.3f }, Vector3f(1.0f), Vector3f(0.0f));
		pEntity->AddTag<LightTag>();
		auto& pointLight = pEntity->AddComponent<PointLightComponent>();
		pointLight.radius = 3.0f;
		pointLight.color = { 0.0f, 1.0f, 0.705f };

		DefaultScene();
		//MaterialScene();
		//MaterialTexturedScene();

		pEntity = CreateEntity("ddgi_volumn");
		auto& ddgiVolumn = pEntity->AddComponent<DDGIVolumnComponent>();
		
		//pEntity = CreateEntityWithMesh("sponza", "sponza_gltf_khronos_fixed_and_split", EMeshType::eGLTF, true);
		//auto& trans = pEntity->GetComponent<TransformComponent>();
		//trans.rotation = { 0.0f, 0.0f, 0.0f };
		//trans.scale = { 0.1f, 0.1f, 0.1f };

		OnUpdate(0.0f);
	}

	void Scene::OnSceneUnLoad()
	{
		SG_PROFILE_FUNCTION();

		// clear all the object id in the mesh.
		gObjectIdAllocator.Reset();
	}

	void Scene::OnUpdate(float deltaTime)
	{
		SG_PROFILE_FUNCTION();

		mpCameraEntity->GetComponent<CameraComponent>().pCamera->OnUpdate(deltaTime);

		//static float totalTime = 0.0f;
		//static float speed = 2.5f;

		//// Do animation
		//auto* pModel = GetEntityByName("model");
		//auto [tag, trans] = pModel->GetComponent<TagComponent, TransformComponent>();
		//tag.bDirty = true;
		//trans.position = { Sin(totalTime) * 0.5f, 0.0f, 0.0f };

		//totalTime += deltaTime * speed;

		UpdateMeshAABB();
		mEntityManager.ReFresh();
	}

	Scene::Entity* Scene::CreateEntity(const string& name)
	{
		return &(CreateEntityContext(name)->entity);
	}

	Scene::Entity* Scene::CreateEntity(const string& name, const Vector3f& pos, const Vector3f& scale, const Vector3f& rot)
	{
		return &(CreateEntityContext(name, pos, scale, rot)->entity);
	}

	Scene::EntityContext* Scene::CreateEntityContext(const string& name)
	{
		SG_PROFILE_FUNCTION();

		auto* pEntityContext = CreateEntityContextWithoutTreeNode(name);
		pEntityContext->pTreeNode = New(TreeNode, &pEntityContext->entity);
		pEntityContext->pTreeNode->pParent = mpRootNode;
		mpRootNode->pChilds.emplace_back(pEntityContext->pTreeNode);

		return pEntityContext;
	}

	Scene::EntityContext* Scene::CreateEntityContext(const string& name, const Vector3f& pos, const Vector3f& scale, const Vector3f& rot)
	{
		SG_PROFILE_FUNCTION();

		auto* pEntityContext = CreateEntityContextWithoutTreeNode(name);
		pEntityContext->pTreeNode = New(TreeNode, &pEntityContext->entity);
		mpRootNode->pChilds.emplace_back(pEntityContext->pTreeNode);
		pEntityContext->pTreeNode->pParent = mpRootNode;

		auto& trans = pEntityContext->entity.GetComponent<TransformComponent>();
		trans.position = pos;
		trans.rotation = rot;
		trans.scale = scale;

		return pEntityContext;
	}

	Scene::Entity* Scene::CreateEntityWithMesh(const string& name, const string& filename, EMeshType type, ELoadMeshFlag flag)
	{
		return &(CreateEntityContextWithMesh(name, filename, type, flag)->entity);
	}

	Scene::EntityContext* Scene::CreateEntityContextWithMesh(const string& name, const string& filename, EMeshType type, ELoadMeshFlag flag)
	{
		SG_PROFILE_FUNCTION();

		if (mEntityContexts.find(name) != mEntityContexts.end())
		{
			SG_LOG_WARN("Already have an entity named: %s", name.c_str());
			return nullptr;
		}

		MeshData meshData = {};
		const SubMeshData* pSubMeshData = nullptr;

		// if the mesh data already loaded, you don't need to set it again
		if (!MeshDataArchive::GetInstance()->HaveMeshData(filename))
		{
			MeshResourceLoader loader;
			if (!loader.LoadFromFile(filename, type, meshData, flag))
				SG_LOG_WARN("Mesh %s load failure!", filename);
		}
		else
		{
			pSubMeshData = MeshDataArchive::GetInstance()->GetData(filename);
		}

		auto* pRootContext = CreateEntityContext(name);
		auto* pRootNode = pRootContext->pTreeNode;

		for (UInt32 i = 0; i < meshData.subMeshDatas.size(); ++i) // for each sub mesh data, create an entity
		{
			auto& subMesh = pSubMeshData ? *pSubMeshData : meshData.subMeshDatas[i];

			auto* pEntityContext = CreateEntityContextWithoutTreeNode(subMesh.subMeshName);
			auto* pSubMeshTreeNode = New(TreeNode, &pEntityContext->entity);
			pEntityContext->pTreeNode = pSubMeshTreeNode;

			// do connect
			pSubMeshTreeNode->pParent = pRootNode;
			pRootNode->pChilds.emplace_back(pSubMeshTreeNode);

			// add necessary components
			auto* pSubMeshEntity = &pEntityContext->entity;
			auto& mat = pSubMeshEntity->AddComponent<MaterialComponent>();
			if (subMesh.materialAssetId != IDAllocator<UInt32>::INVALID_ID) // assign material
			{
				auto pMaterialAsset = MaterialAssetArchive::GetInstance()->GetMaterialAsset(subMesh.materialAssetId);
				mat.materialAsset = pMaterialAsset;
				//mat.materialAssetId = pMaterialAsset->GetAssetID();
				//mat.albedoTex = pMaterialAsset->GetAlbedoTexture();
				//mat.metallicTex = pMaterialAsset->GetMetallicTexture();
				//mat.roughnessTex = pMaterialAsset->GetRoughnessTexture();
				//mat.normalTex = pMaterialAsset->GetNormalTexture();
				//mat.AOTex = pMaterialAsset->GetAOTexture();
			}

			auto& mesh = pSubMeshEntity->AddComponent<MeshComponent>();

			mesh.meshType = type;
			mesh.meshId = MeshDataArchive::GetInstance()->SetData(subMesh);
		}

		return pRootContext;
	}

	void Scene::DestroyEntity(Entity& entity)
	{
		SG_PROFILE_FUNCTION();

		auto& tag = entity.GetComponent<TagComponent>();
		mEntityContexts.erase(tag.name);
		mEntityManager.DestroyEntity(entity);
	}

	void Scene::DestroyEntityByName(const string& name)
	{
		SG_PROFILE_FUNCTION();

		auto* pEntity = GetEntityByName(name);
		mEntityManager.DestroyEntity(*pEntity);
		mEntityContexts.erase(name);
	}

	Scene::Entity* Scene::GetEntityByName(const string& name)
	{
		SG_PROFILE_FUNCTION();

		auto* pContext = GetEntityContextByName(name);

		return pContext ? &(pContext->entity) : nullptr;
	}

	Scene::EntityContext* Scene::GetEntityContextByName(const string& name)
	{
		SG_PROFILE_FUNCTION();

		auto node = mEntityContexts.find(name);
		if (node == mEntityContexts.end())
		{
			SG_LOG_WARN("No entity named: %s", name.c_str());
			return nullptr;
		}

		return &node->second;
	}

	Scene::EntityContext* Scene::CreateEntityContextWithoutTreeNode(const string& name)
	{
		SG_PROFILE_FUNCTION();

		if (mEntityContexts.find(name) != mEntityContexts.end())
		{
			SG_LOG_WARN("Already have an entity named: %s", name.c_str());
			return nullptr;
		}

		auto entity = mEntityManager.CreateEntity();
		// an entity must have a TagComponent and a TransformComponent
		entity.AddComponent<TagComponent>(name);
		entity.AddComponent<TransformComponent>();

		auto& context = mEntityContexts.insert(name).first->second;
		context.entity = entity;

		return &context;
	}

	void Scene::DefaultScene()
	{
		SG_PROFILE_FUNCTION();

		auto* pEntity = CreateEntity("model");
		pEntity->AddComponent<MaterialComponent>("model_mat", Vector3f(1.0f), 0.2f, 0.8f);
		auto& mesh = pEntity->AddComponent<MeshComponent>();
		LoadMesh("model", EMeshType::eOBJ, mesh, ELoadMeshFlag::efGenerateAABB);

		pEntity = CreateEntity("model_1", { 0.0f, 0.0f, -1.5f }, { 0.6f, 0.6f, 0.6f }, Vector3f(0.0f));
		pEntity->AddComponent<MaterialComponent>("model_1_mat", Vector3f(1.0f), 0.8f, 0.35f);
		auto& mesh1 = pEntity->AddComponent<MeshComponent>();
		CopyMesh(GetEntityByName("model")->GetComponent<MeshComponent>(), mesh1);

		pEntity = CreateEntity("grid", Vector3f(0.0f), { 8.0f, 1.0f, 8.0f }, Vector3f(0.0f));
		auto& mat = pEntity->AddComponent<MaterialComponent>("grid_mat", Vector3f(1.0f), 0.05f, 0.76f);
		mat.materialAsset.lock()->SetNormalTexture(TextureAssetArchive::GetInstance()->NewTextureAsset("cerberus_normal", "cerberus/normal.ktx", true));
		auto& mesh2 = pEntity->AddComponent<MeshComponent>();
		LoadMesh(EGennerateMeshType::eGrid, mesh2);
	}

	void Scene::MaterialScene()
	{
		SG_PROFILE_FUNCTION();

		const float zPos = -4.0f;
		const float INTERVAL = 1.5f;
		for (UInt32 i = 0; i < 10; ++i)
		{
			for (UInt32 j = 0; j < 10; ++j)
			{
				string name = "sphere" + eastl::to_string(i) + "_" + eastl::to_string(j);
				if (i == 0 && j == 0)
				{
					auto* pEntity = CreateEntity(name, { -7.0f + i * INTERVAL, -7.0f + j * INTERVAL, zPos }, { 0.7f, 0.7f, 0.7f }, Vector3f(0.0f));
					auto& mesh = pEntity->AddComponent<MeshComponent>();
					LoadMesh("sphere", EMeshType::eOBJ, mesh);

					pEntity->AddComponent<MaterialComponent>(name + "_mat", Vector3f(1.0f), (i + 1) * 0.1f, (10 - j) * 0.1f);
				}
				else
				{
					auto* pEntity = CreateEntity(name, { -7.0f + i * INTERVAL, -7.0f + j * INTERVAL, zPos }, { 0.7f, 0.7f, 0.7f }, Vector3f(0.0f));
					auto& mesh = pEntity->AddComponent<MeshComponent>();

					auto* pCopyEntity = GetEntityByName("sphere0_0");
					auto& srcMesh = pCopyEntity->GetComponent<MeshComponent>();
					CopyMesh(srcMesh, mesh);

					pEntity->AddComponent<MaterialComponent>(name + "_mat", Vector3f(1.0f), (i + 1) * 0.1f, (10 - j) * 0.1f);
				}
			}
		}
	}

	void Scene::MaterialTexturedScene()
	{
		auto* pEntity = CreateEntity("cerberus");
		auto& trans = pEntity->GetComponent<TransformComponent>();
		trans.position.x = 2.7f;
		trans.position.y = 3.0f;
		trans.rotation.y = 90.0f;
		trans.scale = { 3.0f, 3.0f, 3.0f };
		auto& mat = pEntity->AddComponent<MaterialComponent>("cerberus_mat", Vector3f(1.0f), 0.05f, 0.9f);
		auto pMatAsset = mat.materialAsset.lock();
		pMatAsset->SetAlbedoTexture(TextureAssetArchive::GetInstance()->NewTextureAsset("cerberus_albedo", "cerberus/albedo.ktx", true));
		pMatAsset->SetNormalTexture(TextureAssetArchive::GetInstance()->NewTextureAsset("cerberus_normal", "cerberus/normal.ktx", true));
		pMatAsset->SetMetallicTexture(TextureAssetArchive::GetInstance()->NewTextureAsset("cerberus_metallic", "cerberus/metallic.ktx", true));
		pMatAsset->SetRoughnessTexture(TextureAssetArchive::GetInstance()->NewTextureAsset("cerberus_roughness", "cerberus/roughness.ktx", true));
		pMatAsset->SetAOTexture(TextureAssetArchive::GetInstance()->NewTextureAsset("cerberus_ao", "cerberus/ao.ktx", true));
		auto& mesh = pEntity->AddComponent<MeshComponent>();
		LoadMesh("cerberus", EMeshType::eOBJ, mesh);

		pEntity = CreateEntity("cerberus_1");
		auto& trans1 = pEntity->GetComponent<TransformComponent>();
		trans1.position.x = 2.6f;
		trans1.position.y = 1.85f;
		trans1.position.z = -2.3f;
		trans1.rotation.y = 90.0f;
		trans1.scale = { 3.0f, 3.0f, 3.0f };
		auto& mat1 = pEntity->AddComponent<MaterialComponent>("cerberus_1_mat", Vector3f(1.0f), 0.05f, 0.9f);
		auto pMat1Asset = mat1.materialAsset.lock();
		pMat1Asset->SetAOTexture(TextureAssetArchive::GetInstance()->NewTextureAsset("cerberus_ao", "cerberus/ao.ktx", true));
		auto& mesh1 = pEntity->AddComponent<MeshComponent>();
		CopyMesh(GetEntityByName("cerberus")->GetComponent<MeshComponent>(), mesh1);
	}

	void Scene::Serialize(json& node)
	{
		SG_PROFILE_FUNCTION();

		node["Scene"] = "My Default Scene";
		node["Entities"] = {};

		for (auto* pChild : mpRootNode->pChilds)
			SerializeEntity(pChild, node["Entities"].emplace_back());
	}

	void Scene::SerializeEntity(Scene::TreeNode* pTreeNode, json& node)
	{
		auto& entity = *pTreeNode->pEntity;
		node["EntityID"] = "2468517968452275"; // TEMPORARY

		auto& tagNode = node["TagComponent"];
		{
			auto& tag = entity.GetComponent<TagComponent>();
			tagNode["Name"] = tag.name.c_str();
		}

		auto& transNode = node["TransformComponent"];
		{
			auto& trans = entity.GetComponent<TransformComponent>();
			transNode["Position"] = trans.position;
			transNode["Rotation"] = trans.rotation;
			transNode["Scale"] = trans.scale;
		}

		if (entity.HasComponent<MeshComponent>())
		{
			auto& meshNode = node["MeshComponent"];
			{
				auto& mesh = entity.GetComponent<MeshComponent>();
				auto* pMeshData = MeshDataArchive::GetInstance()->GetData(mesh.meshId);
				meshNode["Filename"] = pMeshData->filename.c_str();
				meshNode["SubMeshName"] = pMeshData->subMeshName.c_str();
				meshNode["Type"] = mesh.meshType;
				meshNode["IsProceduralMesh"] = pMeshData->bIsProceduralMesh;
				meshNode["HaveEmbeddedResource"] = pMeshData->materialAssetId != IDAllocator<UInt32>::INVALID_ID;
				if (!pMeshData->bIsProceduralMesh)
				{
					auto& aabb = pMeshData->aabb;
					meshNode["AABB"] = aabb;
				}
			}
		}

		if (entity.HasComponent<MaterialComponent>())
		{
			auto& matNode = node["MaterialComponent"];
			{
				auto& mat = entity.GetComponent<MaterialComponent>();
				auto pMatAsset = mat.materialAsset.lock();
				matNode["AssetName"] = pMatAsset->GetAssetName().c_str();
				matNode["Filename"] = pMatAsset->GetFileName().c_str();

				matNode["Albedo"] = pMatAsset->GetAlbedo();
				matNode["Metallic"] = pMatAsset->GetMetallic();
				matNode["Roughness"] = pMatAsset->GetRoughness();
				UInt32 texMask = pMatAsset->GetTextureMask();
				if ((texMask & MaterialAsset::ALBEDO_TEX_MASK) != 0)
				{
					auto& assetNode = matNode["AlbedoTextureAsset"];
					pMatAsset->GetAlbedoTexture()->Serialize(assetNode);
				}
				if ((texMask & MaterialAsset::METALLIC_TEX_MASK) != 0)
				{
					auto& assetNode = matNode["MetallicTextureAsset"];
					pMatAsset->GetMetallicTexture()->Serialize(assetNode);
				}
				if ((texMask & MaterialAsset::ROUGHNESS_TEX_MASK) != 0)
				{
					auto& assetNode = matNode["RoughnessTextureAsset"];
					pMatAsset->GetRoughnessTexture()->Serialize(assetNode);
				}
				if ((texMask & MaterialAsset::NORMAL_TEX_MASK) != 0)
				{
					auto& assetNode = matNode["NormalTextureAsset"];
					pMatAsset->GetNormalTexture()->Serialize(assetNode);
				}
				if ((texMask & MaterialAsset::AO_TEX_MASK) != 0)
				{
					auto& assetNode = matNode["AOTextureAsset"];
					pMatAsset->GetAOTexture()->Serialize(assetNode);
				}

			}
		}

		if (entity.HasComponent<PointLightComponent>())
		{
			auto& lightNode = node["PointLightComponent"];
			{
				auto& light = entity.GetComponent<PointLightComponent>();
				lightNode["Color"] = light.color;
				lightNode["Radius"] = light.radius;
			}
		}

		if (entity.HasComponent<DirectionalLightComponent>())
		{
			auto& lightNode = node["DirectionalLightComponent"];
			{
				auto& light = entity.GetComponent<DirectionalLightComponent>();
				lightNode["Color"] = light.color;
				lightNode["ShadowMapScaleFactor"] = light.shadowMapScaleFactor;
				lightNode["zNear"] = light.zNear;
				lightNode["zFar"] = light.zFar;
			}
		}

		if (entity.HasComponent<CameraComponent>())
		{
			auto& camNode = node["CameraComponent"];
			{
				auto& cam = entity.GetComponent<CameraComponent>();
				camNode["CameraPos"] = cam.pCamera->GetPosition();
				camNode["CameraMoveSpeed"] = cam.pCamera->GetMoveSpeed();
				if (cam.type == ECameraType::eFirstPerson)
				{
					auto* pFPSCam = static_cast<FirstPersonCamera*>(cam.pCamera.get());
					camNode["UpVector"] = pFPSCam->GetUpVector();
					camNode["RightVector"] = pFPSCam->GetRightVector();
					camNode["FrontVector"] = pFPSCam->GetFrontVector();
				}
			}
		}

		if (!pTreeNode->pChilds.empty()) // this node have children
		{
			node["Children"] = {};
			for (auto* pChild : pTreeNode->pChilds)
				SerializeEntity(pChild, node["Children"].emplace_back());
		}
	}

	void Scene::Deserialize(json& node)
	{
		SG_PROFILE_FUNCTION();

		string sceneName = node["Scene"].get<std::string>().c_str();
		auto& entities = node["Entities"];
		if (entities.is_array() && !entities.empty())
		{
			for (auto& entity : entities)
				DeserializeEntity(mpRootNode, entity);
		}

		Refresh();
	}

	void Scene::DeserializeEntity(Scene::TreeNode* pTreeNode, json& entity)
	{
		auto& tagComp = entity["TagComponent"];
		string name = tagComp["Name"].get<std::string>().c_str();

		auto& transComp = entity["TransformComponent"];
		auto* pEntityContext = CreateEntityContextWithoutTreeNode(name);
		auto* pEntity = &(pEntityContext->entity);
		auto& trans = pEntity->GetComponent<TransformComponent>();
		transComp["Position"].get_to(trans.position);
		transComp["Scale"].get_to(trans.scale);
		transComp["Rotation"].get_to(trans.rotation);

		// create a tree node and do connect
		auto* pCurrTreeNode = New(TreeNode, pEntity);
		pEntityContext->pTreeNode = pCurrTreeNode;
		pCurrTreeNode->pParent = pTreeNode;
		pTreeNode->pChilds.emplace_back(pCurrTreeNode);

		if (auto node = entity.find("MeshComponent"); node != entity.end())
		{
			auto& meshComp = *node;

			string filename = meshComp["Filename"].get<std::string>().c_str();
			string subMeshName = meshComp["SubMeshName"].get<std::string>().c_str();
			bool bIsProceduralMesh = meshComp["IsProceduralMesh"].get<bool>();
			bool bHaveEmbeddedResource = meshComp["HaveEmbeddedResource"].get<bool>();
			auto& mesh = pEntity->AddComponent<MeshComponent>();

			if (bIsProceduralMesh)
			{
				if (filename == "_generated_grid")
					LoadMesh(EGennerateMeshType::eGrid, mesh);
				else if (filename == "_generated_skybox")
					LoadMesh(EGennerateMeshType::eSkybox, mesh);
			}
			else
			{
				EMeshType type = (EMeshType)meshComp["Type"];
				ELoadMeshFlag flag = bHaveEmbeddedResource ? ELoadMeshFlag::efLoadEmbeddedMaterials : ELoadMeshFlag(0);
				if (meshComp.find("AABB") == meshComp.end())
					flag |= ELoadMeshFlag::efGenerateAABB;
				LoadMesh(filename.c_str(), subMeshName.c_str(), type, mesh, flag);

				if (meshComp.find("AABB") != meshComp.end())
				{
					auto* pSubMeshData = MeshDataArchive::GetInstance()->GetData(subMeshName);
					meshComp["AABB"].get_to(pSubMeshData->aabb);
				}
			}
		}

		if (auto node = entity.find("MaterialComponent"); node != entity.end())
		{
			auto& matComp = *node;

			auto& mat = pEntity->AddComponent<MaterialComponent>();

			string assetName = matComp["AssetName"].get<std::string>().c_str();
			string filename = matComp["Filename"].get<std::string>().c_str();

			if (!filename.empty()) // it is a embedded material
			{
				mat.materialAsset = MaterialAssetArchive::GetInstance()->GetMaterialAsset(assetName);
				auto materialAsset = mat.materialAsset.lock();

				materialAsset->SetAlbedo(matComp["Albedo"].get<Vector3f>());
				materialAsset->SetMetallic(matComp["Metallic"].get<float>());
				materialAsset->SetRoughness(matComp["Roughness"].get<float>());
			}
			else // create an asset
			{
				auto materialAsset = MaterialAssetArchive::GetInstance()->NewMaterialAsset(assetName, filename);
				mat.materialAsset = materialAsset;

				materialAsset->SetAlbedo(matComp["Albedo"].get<Vector3f>());
				materialAsset->SetMetallic(matComp["Metallic"].get<float>());
				materialAsset->SetRoughness(matComp["Roughness"].get<float>());

				if (auto n = matComp.find("AlbedoTextureAsset"); n != matComp.end())
				{
					auto& node = *n;
					string texFilename = node["Filename"].get<std::string>().c_str();
					if (texFilename != filename) // it is not an embedded textures, loaded it explicitly
					{
						materialAsset->SetAlbedoTexture(TextureAssetArchive::GetInstance()->NewTextureAsset("", texFilename));
						materialAsset->GetAlbedoTexture()->Deserialize(node);
					}
					else // it is an embedded textures, check to see if it is successfully loaded
					{
						SG_ASSERT(materialAsset->GetAlbedoTexture()->GetFileName() == texFilename);
					}
				}

				if (auto n = matComp.find("MetallicTextureAsset"); n != matComp.end())
				{
					auto& node = *n;
					string texFilename = node["Filename"].get<std::string>().c_str();
					if (texFilename != filename) // it is not an embedded textures, loaded it explicitly
					{
						materialAsset->SetMetallicTexture(TextureAssetArchive::GetInstance()->NewTextureAsset("", texFilename));
						materialAsset->GetMetallicTexture()->Deserialize(node);
					}
					else // it is an embedded textures, check to see if it is successfully loaded
					{
						SG_ASSERT(materialAsset->GetMetallicTexture()->GetFileName() == texFilename);
					}
				}

				if (auto n = matComp.find("RoughnessTextureAsset"); n != matComp.end())
				{
					auto& node = *n;
					string texFilename = node["Filename"].get<std::string>().c_str();
					if (texFilename != filename) // it is not an embedded textures, loaded it explicitly
					{
						materialAsset->SetRoughnessTexture(TextureAssetArchive::GetInstance()->NewTextureAsset("", texFilename));
						materialAsset->GetRoughnessTexture()->Deserialize(node);
					}
					else // it is an embedded textures, check to see if it is successfully loaded
					{
						SG_ASSERT(materialAsset->GetRoughnessTexture()->GetFileName() == texFilename);
					}
				}

				if (auto n = matComp.find("NormalTextureAsset"); n != matComp.end())
				{
					auto& node = *n;
					string texFilename = node["Filename"].get<std::string>().c_str();
					if (texFilename != filename) // it is not an embedded textures, loaded it explicitly
					{
						materialAsset->SetNormalTexture(TextureAssetArchive::GetInstance()->NewTextureAsset("", texFilename));
						materialAsset->GetNormalTexture()->Deserialize(node);
					}
					else // it is an embedded textures, check to see if it is successfully loaded
					{
						SG_ASSERT(materialAsset->GetNormalTexture()->GetFileName() == texFilename);
					}
				}

				if (auto n = matComp.find("AOTextureAsset"); n != matComp.end())
				{
					auto& node = *n;
					string texFilename = node["Filename"].get<std::string>().c_str();
					if (texFilename != filename) // it is not an embedded textures, loaded it explicitly
					{
						materialAsset->SetAOTexture(TextureAssetArchive::GetInstance()->NewTextureAsset("", texFilename));
						materialAsset->GetAOTexture()->Deserialize(node);
					}
					else // it is an embedded textures, check to see if it is successfully loaded
					{
						SG_ASSERT(materialAsset->GetAOTexture()->GetFileName() == texFilename);
					}
				}
			}
		}

		if (auto node = entity.find("PointLightComponent"); node != entity.end())
		{
			auto& pointLightComp = *node;

			auto& light = pEntity->AddComponent<PointLightComponent>();
			pointLightComp["Color"].get_to<Vector3f>(light.color);
			pointLightComp["Radius"].get_to(light.radius);
		}

		if (auto node = entity.find("DirectionalLightComponent"); node != entity.end())
		{
			auto& directionalLightComp = *node;

			auto& light = pEntity->AddComponent<DirectionalLightComponent>();
			directionalLightComp["Color"].get_to(light.color);
			directionalLightComp["ShadowMapScaleFactor"].get_to(light.shadowMapScaleFactor);
			directionalLightComp["zNear"].get_to(light.zNear);
			directionalLightComp["zFar"].get_to(light.zFar);
		}

		if (auto node = entity.find("CameraComponent"); node != entity.end())
		{
			auto& cameraComp = *node;
			auto& cam = pEntity->AddComponent<CameraComponent>();

			cam.type = ECameraType::eFirstPerson;
			auto FPSCam = MakeRef<FirstPersonCamera>(cameraComp["CameraPos"].get<Vector3f>());
			FPSCam->SetUpVector(cameraComp["UpVector"].get<Vector3f>());
			FPSCam->SetRightVector(cameraComp["RightVector"].get<Vector3f>());
			FPSCam->SetFrontVector(cameraComp["FrontVector"].get<Vector3f>());
			cam.pCamera = FPSCam;
			cam.pCamera->SetMoveSpeed(cameraComp["CameraMoveSpeed"].get<float>());
			mpCameraEntity = pEntity;
		}

		// if this node have child
		auto& children = entity["Children"];
		if (children.is_array() && !children.empty())
		{
			for (auto& child : children)
				DeserializeEntity(pCurrTreeNode, child);
		}
	}

	void Scene::Refresh()
	{
		SG_PROFILE_FUNCTION();

		mEntityManager.ReFresh();

		mMeshEntityCount = 0;
		for (auto node : mEntityContexts)
		{
			auto& entity = node.second.entity;
			if (entity.HasComponent<MeshComponent>())
				++mMeshEntityCount;
		}
	}

	void Scene::ClearTreeNodes()
	{
		SG_PROFILE_FUNCTION();

		FreeTreeNode(mpRootNode);
	}

	void Scene::FreeTreeNode(TreeNode* pTreeNode)
	{
		SG_PROFILE_FUNCTION();

		if (pTreeNode->pChilds.empty()) // it is a leaf node
		{
			Delete(pTreeNode);
			return;
		}

		for (auto* pChild : pTreeNode->pChilds)
			FreeTreeNode(pChild);
		Delete(pTreeNode);
	}

	void Scene::UpdateMeshAABB()
	{
		// update AABB for the meshes
		auto view = View<TagComponent, TransformComponent, MeshComponent>();
		for (auto& entity : view)
		{
			auto [tag, trans, mesh] = entity.GetComponent<TagComponent, TransformComponent, MeshComponent>();
			if (tag.bDirty)
			{
				const auto* pSubMeshData = MeshDataArchive::GetInstance()->GetData(mesh.meshId);
				const auto* pEntityContext = GetEntityContextByName(tag.name);
				mesh.aabb = AABBTransform(pSubMeshData->aabb, GetTransform(pEntityContext->pTreeNode));
			}
			// here we ain't going to mark dirty flag as false, because the renderer will use it again
			// the renderer will mark it as false
		}
	}

}