#include "ofbxImp.h"

namespace ofbx
{

	template <typename T>
	static void generateIndices(
		std::vector<int>* indices,
		const std::vector<T>& data,
		GeometryImpl::VertexDataMapping mapping,
		const std::vector<int>& vertiex_indices)
	{
		assert(indices);
		assert(!data.empty());
		assert(!vertiex_indices.empty());

		if (!indices->empty())
		{
			assert(indices->size() == vertiex_indices.size());
			return;
		}

		size_t count = vertiex_indices.size();
		indices->resize(count);
		if (mapping == GeometryImpl::BY_POLYGON_VERTEX)
		{
			for (int i = 0; i < count; ++i)
			{
				(*indices)[i] = i;
			}
		}
		else if (mapping == GeometryImpl::BY_VERTEX)
		{
			memcpy(&(*indices)[0], &vertiex_indices[0], sizeof(vertiex_indices[0]) * count);
		}
		else {
			assert(false);
		}
	}


	class VertexData {
		int pos;
		int nrm;
		int tan;
		int clr;
		int uv;

	public:
		VertexData() {}

		enum EXCLUDE {
			EXCLUDE_VERTEX  = 0x01,
			EXCLUDE_NORMAL  = 0x02,
			EXCLUDE_TANGENT = 0x04,
			EXCLUDE_COLOR   = 0x08,
			EXCLUDE_UV		= 0x10,
		};

		VertexData(const GeometryImpl& geom, size_t index, int mask) {
			pos = geom.vertex_indices.empty() || (mask&EXCLUDE_VERTEX) ?
				-1 : geom.vertex_indices[index];

			nrm = geom.normal_indices.empty() || (mask&EXCLUDE_NORMAL) ?
				-1 : geom.normal_indices[index];

			tan = geom.tangent_indices.empty() || (mask&EXCLUDE_TANGENT) ?
				-1 : geom.tangent_indices[index];

			clr = geom.color_indices.empty() || (mask&EXCLUDE_COLOR) ?
				-1 : geom.color_indices[index];

			uv =  geom.uv_indices.empty() || (mask&EXCLUDE_UV) ?
				-1 : geom.uv_indices[index];
		}

		// overload operator==
		bool operator!=(const VertexData& p) {
			return this->pos != p.pos ||
				this->nrm != p.nrm ||
				this->tan != p.tan ||
				this->clr != p.clr ||
				this->uv != p.uv;
		}
	};


	template <typename T>
	static void expand(std::vector<T>& data, std::vector<int>& indices, const GeometryImpl& geom, int mask)
	{
		std::unordered_map<size_t, VertexData> map;
		std::unordered_map<size_t, VertexData>::iterator it;

		const size_t count = indices.size();
		map.reserve(count);

		VertexData vtx(geom, 0, mask);
		map[indices[0]] = vtx;

		for (size_t i = 1; i < count; ++i) {
			size_t idx = indices[i];
			VertexData vtx(geom, i, mask);
			it = map.find(idx);
			if (it == map.end()) {
				map[idx] = vtx;
			}
			else if (it->second != vtx) {
				// expand data
				int new_idx = (int)data.size();
				data.push_back(data[idx]);
				indices[i] = new_idx;
				map[new_idx] = vtx;
			}
		}
	}

	template <typename T>
	static void remapForRendering(std::vector<T>* out, const std::vector<int>& indices, const std::vector<int>& mapping)
	{
		if (out->empty()) return;

		std::vector<T> old(*out);
		for (size_t i = 0, c = indices.size(); i < c; ++i)
		{
			const int idx = mapping[i];
			const int ref = indices[i];
			(*out)[idx] = old[ref];
		}
	}

	OptionalError<Object*> parseGeometryForRendering(const Scene& scene, const Element& element)
	{
		assert(element.first_property);

		const Element* vertices_element = findChild(element, "Vertices");
		if (!vertices_element || !vertices_element->first_property) return Error("Vertices missing");

		const Element* polys_element = findChild(element, "PolygonVertexIndex");
		if (!polys_element || !polys_element->first_property) return Error("Indices missing");

		std::unique_ptr<GeometryImpl> geom = std::make_unique<GeometryImpl>(scene, element);

		if (!parseDoubleVecData(*vertices_element->first_property, &geom->vertices)) return Error("Failed to parse vertices");

		if (!parseBinaryArray(*polys_element->first_property, &geom->vertex_indices)) return Error("Failed to parse indices");

		std::vector<int> to_old_indices;
		geom->triangulate(geom->vertex_indices, &geom->triangles, &to_old_indices);

		const Element* layer_material_element = findChild(element, "LayerElementMaterial");
		if (layer_material_element)
		{
			const Element* mapping_element = findChild(*layer_material_element, "MappingInformationType");
			const Element* reference_element = findChild(*layer_material_element, "ReferenceInformationType");

			std::vector<int> tmp;

			if (!mapping_element || !reference_element) return Error("Invalid LayerElementMaterial");

			if (mapping_element->first_property->value == "ByPolygon" &&
				reference_element->first_property->value == "IndexToDirect")
			{
				geom->materials.reserve(geom->vertices.size() / 3);
				for (int& i : geom->materials) i = -1;

				const Element* indices_element = findChild(*layer_material_element, "Materials");
				if (!indices_element || !indices_element->first_property) return Error("Invalid LayerElementMaterial");

				if (!parseBinaryArray(*indices_element->first_property, &tmp)) return Error("Failed to parse material indices");

				int tmp_i = 0;
				for (int poly = 0, c = (int)tmp.size(); poly < c; ++poly)
				{
					int tri_count = getTriCountFromPoly(geom->vertex_indices, &tmp_i);
					for (int i = 0; i < tri_count; ++i)
					{
						geom->materials.push_back(tmp[poly]);
					}
				}
			}
			else
			{
				if (mapping_element->first_property->value != "AllSame") return Error("Mapping not supported");
			}
		}
		
		size_t max_count = geom->vertices.size();
		const Element* layer_uv_element = findChild(element, "LayerElementUV");
		GeometryImpl::VertexDataMapping mapping;
		if (layer_uv_element)
		{
			if (!parseVertexData(*layer_uv_element, "UV", "UVIndex", &geom->uvs, &geom->uv_indices, &mapping))
				return Error("Invalid UVs");
			generateIndices(&geom->uv_indices, geom->uvs, mapping, geom->vertex_indices);
		}

		const Element* layer_tangent_element = findChild(element, "LayerElementTangents");
		if (layer_tangent_element)
		{
			if (findChild(*layer_tangent_element, "Tangents"))
			{
				if (!parseVertexData(*layer_tangent_element, "Tangents", "TangentsIndex", &geom->tangents, &geom->tangent_indices, &mapping))
					return Error("Invalid tangets");
			}
			else
			{
				if (!parseVertexData(*layer_tangent_element, "Tangent", "TangentIndex", &geom->tangents, &geom->tangent_indices, &mapping))
					return Error("Invalid tangets");
			}
			generateIndices(&geom->tangent_indices, geom->tangents, mapping, geom->vertex_indices);
		}

		const Element* layer_color_element = findChild(element, "LayerElementColor");
		if (layer_color_element)
		{
			if (!parseVertexData(*layer_color_element, "Colors", "ColorIndex", &geom->colors, &geom->color_indices, &mapping))
				return Error("Invalid colors");
			generateIndices(&geom->color_indices, geom->colors, mapping, geom->vertex_indices);
		}

		const Element* layer_normal_element = findChild(element, "LayerElementNormal");
		if (layer_normal_element)
		{
			if (!parseVertexData(*layer_normal_element, "Normals", "NormalsIndex", &geom->normals, &geom->normal_indices, &mapping))
				return Error("Invalid normals");
			generateIndices(&geom->normal_indices, geom->normals, mapping, geom->vertex_indices);
		}

		// remap attributes to align vertex indices and expand buffer for rendering
		const GeometryImpl& geomImpl = *geom.get();
		expand(geom->vertices, geom->vertex_indices, geomImpl, VertexData::EXCLUDE_VERTEX);

		if (!geom->normals.empty()) {
			expand(geom->normals, geom->normal_indices, geomImpl, VertexData::EXCLUDE_NORMAL);
			// remap other attributes by vertex indices
			remapForRendering(&geom->normals, geom->normal_indices, geom->vertex_indices);
		}

		if (!geom->tangents.empty()) {
			expand(geom->tangents, geom->tangent_indices, geomImpl, VertexData::EXCLUDE_TANGENT);
			// remap other attributes by vertex indices
			remapForRendering(&geom->tangents, geom->tangent_indices, geom->vertex_indices);
		}

		if (!geom->colors.empty()) {
			expand(geom->colors, geom->color_indices, geomImpl, VertexData::EXCLUDE_COLOR);
			// remap other attributes by vertex indices
			remapForRendering(&geom->colors, geom->color_indices, geom->vertex_indices);
		}

		if (!geom->uvs.empty()) {
			expand(geom->uvs, geom->uv_indices, geomImpl, VertexData::EXCLUDE_UV);
			// remap other attributes by vertex indices
			remapForRendering(&geom->uvs, geom->uv_indices, geom->vertex_indices);
		}

		// remap triangle indices
		size_t count = geom->triangles.size();
		for (int i = 0; i < count; ++i) {
			geom->triangles[i] = geom->vertex_indices[to_old_indices[i]];
		}

		return geom.release();
	}

}//namespace
