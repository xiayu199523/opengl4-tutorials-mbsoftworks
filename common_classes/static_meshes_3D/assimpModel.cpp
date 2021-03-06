// Project
#include "assimpModel.h"
#include "../stringUtils.h"
#include "../textureManager.h"

// Assimp
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

namespace static_meshes_3D {

AssimpModel::AssimpModel(const std::string& filePath, const std::string& defaultTextureName, bool withPositions, bool withTextureCoordinates, bool withNormals)
	: StaticMesh3D(withPositions, withTextureCoordinates, withNormals)
{
	loadModelFromFile(filePath, defaultTextureName);
}

AssimpModel::AssimpModel(const std::string& filePath, bool withPositions, bool withTextureCoordinates, bool withNormals)
	: StaticMesh3D(withPositions, withTextureCoordinates, withNormals)
{
	loadModelFromFile(filePath);
}

bool AssimpModel::loadModelFromFile(const std::string& filePath, const std::string& defaultTextureName)
{
	if (_isInitialized) {
		deleteMesh();
	}

	Assimp::Importer importer;
	const aiScene* scene = importer.ReadFile(filePath,
		aiProcess_CalcTangentSpace |
		aiProcess_GenSmoothNormals |
		aiProcess_Triangulate |
		aiProcess_JoinIdenticalVertices |
		aiProcess_SortByPType);

	if (!scene) {
		return false;
	}

	_modelRootDirectoryPath = string_utils::getDirectoryPath(filePath);

	glGenVertexArrays(1, &_vao);
	glBindVertexArray(_vao);

	_vbo.createVBO();
	_vbo.bindVBO();

	const auto vertexByteSize = sizeof(aiVector3D) * 2 + sizeof(aiVector2D);
	auto vertexCount = 0;

	if (hasPositions())
	{
		for (auto i = 0; i < scene->mNumMeshes; i++)
		{
			const auto meshPtr = scene->mMeshes[i];
			auto vertexCountMesh = 0;
			_meshStartIndices.push_back(vertexCount);
			_meshMaterialIndices.push_back(meshPtr->mMaterialIndex);

			for (auto j = 0; j < meshPtr->mNumFaces; j++)
			{
				const auto& face = meshPtr->mFaces[j];
				if (face.mNumIndices != 3) {
					continue; // Skip non-triangle faces for now
				}

				for (auto k = 0; k < face.mNumIndices; k++)
				{
					const auto& position = meshPtr->mVertices[face.mIndices[k]];
					_vbo.addData(&position, sizeof(aiVector3D));
				}

				vertexCountMesh += face.mNumIndices;
			}

			vertexCount += vertexCountMesh;
			_meshVerticesCount.push_back(vertexCountMesh);
		}
	}

	if (hasTextureCoordinates())
	{
		for (auto i = 0; i < scene->mNumMeshes; i++)
		{
			const auto meshPtr = scene->mMeshes[i];
			for (auto j = 0; j < meshPtr->mNumFaces; j++)
			{
				const auto& face = meshPtr->mFaces[j];
				if (face.mNumIndices != 3) {
					continue; // Skip non-triangle faces for now
				}

				for (auto k = 0; k < face.mNumIndices; k++)
				{
					const auto& textureCoord = meshPtr->mTextureCoords[0][face.mIndices[k]];
					_vbo.addData(&textureCoord, sizeof(aiVector2D));
				}
			}
		}
	}
	
	if (hasNormals())
	{
		for (auto i = 0; i < scene->mNumMeshes; i++)
		{
			const auto meshPtr = scene->mMeshes[i];
			for (auto j = 0; j < meshPtr->mNumFaces; j++)
			{
				const auto& face = meshPtr->mFaces[j];
				if (face.mNumIndices != 3) {
					continue; // Skip non-triangle faces for now
				}

				for (auto k = 0; k < face.mNumIndices; k++)
				{
					const auto& normal = meshPtr->HasNormals() ? meshPtr->mNormals[face.mIndices[k]] : aiVector3D(1.0f, 1.0f, 1.0f);
					_vbo.addData(&normal, sizeof(aiVector3D));
				}
			}
		}
	}

	for(auto i = 0; i < scene->mNumMaterials; i++)
	{
		const auto materialPtr = scene->mMaterials[i];
		aiString aiTexturePath;
		// TODO: this here is a hack for 64-bit system - Assimp has a problem extracting texture path apparently
		// On 64-bit version, some models report texture count 1 and then it crashes when getting them
		if (defaultTextureName.empty() && materialPtr->GetTextureCount(aiTextureType_DIFFUSE) > 0)
		{
			if (materialPtr->GetTexture(aiTextureType_DIFFUSE, 0, &aiTexturePath) == AI_SUCCESS)
			{
				const std::string textureFileName = aiStringToStdString(aiTexturePath);
				loadMaterialTexture(i, textureFileName);
			}
		}
	}

	if (!defaultTextureName.empty()) {
		loadMaterialTexture(0, defaultTextureName);
	}

	_vbo.uploadDataToGPU(GL_STATIC_DRAW);
	setVertexAttributesPointers(vertexCount);
	_isInitialized = true;

	return _isInitialized;
}

void AssimpModel::render() const
{
	if (!_isInitialized) {
		return;
	}

	glBindVertexArray(_vao);

	std::string lastUsedTextureKey = "";
	for(auto i = 0; i < _meshStartIndices.size(); i++)
	{
		const auto usedMaterialIndex = _meshMaterialIndices[i];
		if (_materialTextureKeys.count(usedMaterialIndex) > 0)
		{
			const auto textureKey = _materialTextureKeys.at(usedMaterialIndex);
			if (textureKey != lastUsedTextureKey) {
				TextureManager::getInstance().getTexture(textureKey).bind();
			}
		}

		glDrawArrays(GL_TRIANGLES, _meshStartIndices[i], _meshVerticesCount[i]);
	}
}

void AssimpModel::renderPoints() const
{
	if (!_isInitialized) {
		return;
	}

	glBindVertexArray(_vao);
	for (auto i = 0; i < _meshStartIndices.size(); i++) {
		glDrawArrays(GL_POINTS, _meshStartIndices[i], _meshVerticesCount[i]);
	}
}

void AssimpModel::loadMaterialTexture(const int materialIndex, const std::string& textureFileName)
{
	// If the texture with such path is already loaded, just use it and go on
	const auto fullTexturePath = _modelRootDirectoryPath + textureFileName;
	const auto textureKey = TextureManager::getInstance().containsTextureWithPath(fullTexturePath);
	if (textureKey != "")
	{
		_materialTextureKeys[materialIndex] = textureKey;
		return;
	}

	// Otherwise load this texture and store it in the manager
	const auto newTextureKey = "assimp_" + fullTexturePath;
	TextureManager::getInstance().loadTexture2D(newTextureKey, fullTexturePath);
	_materialTextureKeys[materialIndex] = newTextureKey;
}

std::string AssimpModel::aiStringToStdString(const aiString& aiStringStruct)
{
	auto dataPtr = aiStringStruct.data;
	while (*dataPtr == 0) {
		dataPtr++;
	}

	return dataPtr;
}

} // namespace static_meshes_3D