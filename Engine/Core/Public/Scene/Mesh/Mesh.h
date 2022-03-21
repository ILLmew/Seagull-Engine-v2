#pragma once

#include "Render/MeshGenerate/MeshGenerator.h"
#include "Archive/ResourceLoader/ResourceDefs.h"
#include "Scene/Components.h"

#include "Stl/vector.h"

namespace SG
{

	class Mesh : public TransformComponent
	{
	public:
		explicit Mesh(const char* name, EGennerateMeshType type);
		explicit Mesh(const char* name, EMeshType type);
		Mesh(const char* name, const vector<float>& vertices, const vector<UInt32>& indices);
		~Mesh() = default;

		const string&   GetName() const noexcept { return mName; }
		const EMeshType GetType() const noexcept { return mType; }

		const vector<float>&  GetVertices() const noexcept { return mVertices; }
		const vector<UInt32>& GetIndices()  const noexcept { return mIndices; }
	private:
		string         mName;
		EMeshType      mType;
		vector<float>  mVertices;
		vector<UInt32> mIndices;
	};

}