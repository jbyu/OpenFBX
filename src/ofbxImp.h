#pragma once

#include "ofbx.h"
#include <cassert>
#include <unordered_map>
#include <memory>

namespace ofbx
{
	template <int SIZE> 
	static bool copyString(char(&destination)[SIZE], const char* source)
	{
		const char* src = source;
		char* dest = destination;
		int length = SIZE;
		if (!src) return false;

		while (*src && length > 1)
		{
			*dest = *src;
			--length;
			++dest;
			++src;
		}
		*dest = 0;
		return *src == '\0';
	}


	struct Error
	{
		Error() {}
		Error(const char* msg) { s_message = msg; }

		static const char* s_message;
	};


	template <typename T> 
	struct OptionalError
	{
		OptionalError(Error error)
			: is_error(true)
		{
		}


		OptionalError(T _value)
			: value(_value)
			, is_error(false)
		{
		}


		T getValue() const
		{
#ifdef _DEBUG
			assert(error_checked);
#endif
			return value;
		}


		bool isError()
		{
#ifdef _DEBUG
			error_checked = true;
#endif
			return is_error;
		}


	private:
		T value;
		bool is_error;
#ifdef _DEBUG
		bool error_checked = false;
#endif
	};


	struct Property;
	template <typename T> bool parseBinaryArrayRaw(const Property& property, T* out, int max_size);
	template <typename T> bool parseBinaryArray(Property& property, std::vector<T>* out);


	struct Property : IElementProperty
	{
		~Property() { delete next; }
		Type getType() const override { return (Type)type; }
		IElementProperty* getNext() const override { return next; }
		DataView getValue() const override { return value; }
		int getCount() const override
		{
			assert(type == ARRAY_DOUBLE || type == ARRAY_INT || type == ARRAY_FLOAT || type == ARRAY_LONG);
			return int(*(u32*)value.begin);
		}

		bool getValues(double* values, int max_size) const override { return parseBinaryArrayRaw(*this, values, max_size); }

		bool getValues(float* values, int max_size) const override { return parseBinaryArrayRaw(*this, values, max_size); }

		bool getValues(u64* values, int max_size) const override { return parseBinaryArrayRaw(*this, values, max_size); }

		bool getValues(int* values, int max_size) const override { return parseBinaryArrayRaw(*this, values, max_size); }

		u8 type;
		DataView value;
		Property* next = nullptr;
	};


	struct Element : IElement
	{
		IElement* getFirstChild() const override { return child; }
		IElement* getSibling() const override { return sibling; }
		DataView getID() const override { return id; }
		IElementProperty* getFirstProperty() const override { return first_property; }
		IElementProperty* getProperty(int idx) const
		{
			IElementProperty* prop = first_property;
			for (int i = 0; i < idx; ++i)
			{
				if (prop == nullptr) return nullptr;
				prop = prop->getNext();
			}
			return prop;
		}

		DataView id;
		Element* child = nullptr;
		Element* sibling = nullptr;
		Property* first_property = nullptr;
	};


	struct Root : Object
	{
		Root(const Scene& _scene, const IElement& _element)
			: Object(_scene, _element)
		{
			copyString(name, "RootNode");
			is_node = true;
		}
		Type getType() const override { return Type::ROOT; }
	};


	struct Scene : IScene
	{
		struct Connection
		{
			enum Type
			{
				OBJECT_OBJECT,
				OBJECT_PROPERTY
			};

			Type type;
			u64 from;
			u64 to;
			DataView property;
		};

		struct ObjectPair
		{
			const Element* element;
			Object* object;
		};


		int getAnimationStackCount() const { return (int)m_animation_stacks.size(); }
		int getMeshCount() const override { return (int)m_meshes.size(); }


		const Object* const* getAllObjects() const override { return m_all_objects.empty() ? nullptr : &m_all_objects[0]; }


		int getAllObjectCount() const override { return (int)m_all_objects.size(); }


		const AnimationStack* getAnimationStack(int index) const override
		{
			assert(index >= 0);
			assert(index < m_animation_stacks.size());
			return m_animation_stacks[index];
		}


		const Mesh* getMesh(int index) const override
		{
			assert(index >= 0);
			assert(index < m_meshes.size());
			return m_meshes[index];
		}


		const TakeInfo* getTakeInfo(const char* name) const override
		{
			for (const TakeInfo& info : m_take_infos)
			{
				if (info.name == name) return &info;
			}
			return nullptr;
		}


		const IElement* getRootElement() const override { return m_root_element; }
		const Object* getRoot() const override { return m_root; }

		void destroy() override { delete this; }

		virtual ~Scene();
		
		Element* m_root_element = nullptr;
		Root* m_root = nullptr;
		std::unordered_map<u64, ObjectPair> m_object_map;
		std::vector<Object*> m_all_objects;
		std::vector<Mesh*> m_meshes;
		std::vector<AnimationStack*> m_animation_stacks;
		std::vector<Connection> m_connections;
		std::vector<u8> m_data;
		std::vector<TakeInfo> m_take_infos;
	};


	struct GeometryImpl : Geometry
	{
		enum VertexDataMapping
		{
			BY_POLYGON_VERTEX,
			BY_POLYGON,
			BY_VERTEX
		};

		struct NewVertex
		{
			~NewVertex() { delete next; }

			int index = -1;
			NewVertex* next = nullptr;
		};

		std::vector<Vec3> vertices;
		std::vector<Vec3> normals;

		// only support one uv coordinate
		std::vector<Vec2> uvs;
		std::vector<Vec4> colors;
		std::vector<Vec3> tangents;
		std::vector<int> materials;

		const Skin* skin = nullptr;

		std::vector<int> to_old_vertices;
		std::vector<NewVertex> to_new_vertices;

		std::vector<int> vertex_indices;
		std::vector<int> normal_indices;
		std::vector<int> uv_indices;
		std::vector<int> color_indices;
		std::vector<int> tangent_indices;
		std::vector<int> triangles;

		GeometryImpl(const Scene& _scene, const IElement& _element)
			: Geometry(_scene, _element)
		{
		}


		Type getType() const override { return Type::GEOMETRY; }

		const std::vector<Vec3>& getVertices() const override { return vertices; }
		const std::vector<Vec3>& getNormals() const override { return normals; }
		const std::vector<Vec2>& getUVs() const override { return uvs; }
		const std::vector<Vec4>& getColors() const override { return colors; }
		const std::vector<Vec3>& getTangents() const override { return tangents; }

		const Skin* getSkin() const override { return skin; }
		const int* getMaterials() const override { return materials.empty() ? nullptr : &materials[0]; }

		const std::vector<int>& getTriangles() const override { return triangles; }
		size_t getTriangleCount() const override { return triangles.size() / 3; }

		void triangulate(std::vector<int>& old_indices, std::vector<int>* indices, std::vector<int>* to_old)
		{
			assert(indices);
			assert(to_old);

			auto getIdx = [&old_indices](int i) -> int {
				int idx = old_indices[i];
				return idx < 0 ? -idx - 1 : idx;
			};

			int in_polygon_idx = 0;
			for (int i = 0; i < old_indices.size(); ++i)
			{
				int idx = getIdx(i);
				if (in_polygon_idx <= 2)
				{
					indices->push_back(idx);
					to_old->push_back(i);
				}
				else
				{
					indices->push_back(old_indices[i - in_polygon_idx]);
					to_old->push_back(i - in_polygon_idx);
					indices->push_back(old_indices[i - 1]);
					to_old->push_back(i - 1);
					indices->push_back(idx);
					to_old->push_back(i);
				}
				++in_polygon_idx;
				if (old_indices[i] < 0)
				{
					old_indices[i] = idx;
					in_polygon_idx = 0;
				}
			}
		}
	};

	inline u32 getArrayCount(const Property& property)
	{
		return *(const u32*)property.value.begin;
	}

	bool decompress(const u8* in, size_t in_size, u8* out, size_t out_size);

	template <typename T> 
	static bool parseBinaryArrayRaw(const Property& property, T* out, int max_size)
	{
		assert(out);

		int elem_size = 1;
		switch (property.type)
		{
		case 'l': elem_size = 8; break;
		case 'd': elem_size = 8; break;
		case 'f': elem_size = 4; break;
		case 'i': elem_size = 4; break;
		default: return false;
		}

		const u8* data = property.value.begin + sizeof(u32) * 3;
		if (data > property.value.end) return false;

		u32 count = getArrayCount(property);
		u32 enc = *(const u32*)(property.value.begin + 4);
		u32 len = *(const u32*)(property.value.begin + 8);

		if (enc == 0)
		{
			if ((int)len > max_size) return false;
			if (data + len > property.value.end) return false;
			memcpy(out, data, len);
			return true;
		}
		else if (enc == 1)
		{
			if (int(elem_size * count) > max_size) return false;
			return decompress(data, len, (u8*)out, elem_size * count);
		}

		return false;
	}

	template <typename T>
	static bool parseBinaryArray(Property& property, std::vector<T>* out)
	{
		assert(out);
		u32 count = getArrayCount(property);
		int elem_size = 1;
		switch (property.type)
		{
		case 'd': elem_size = 8; break;
		case 'f': elem_size = 4; break;
		case 'i': elem_size = 4; break;
		default: return false;
		}
		int elem_count = sizeof(T) / elem_size;
		out->resize(count / elem_count);

		return parseBinaryArrayRaw(property, &(*out)[0], int(sizeof((*out)[0]) * out->size()));
	}


	template <typename T> 
	static bool parseDoubleVecData(Property& property, std::vector<T>* out_vec)
	{
		assert(out_vec);
		if (property.type == 'd')
		{
			return parseBinaryArray(property, out_vec);
		}

		assert(property.type == 'f');
		assert(sizeof((*out_vec)[0].x) == sizeof(double));
		std::vector<float> tmp;
		if (!parseBinaryArray(property, &tmp)) return false;
		int elem_count = sizeof((*out_vec)[0]) / sizeof((*out_vec)[0].x);
		out_vec->resize(tmp.size() / elem_count);
		double* out = &(*out_vec)[0].x;
		for (int i = 0, c = (int)tmp.size(); i < c; ++i)
		{
			out[i] = tmp[i];
		}
		return true;
	}


	template <typename T>
	static bool parseVertexData(const Element& element,
		const char* name,
		const char* index_name,
		std::vector<T>* out,
		std::vector<int>* out_indices,
		GeometryImpl::VertexDataMapping* mapping)
	{
		assert(out);
		assert(mapping);
		const Element* data_element = findChild(element, name);
		if (!data_element || !data_element->first_property) 	return false;

		const Element* mapping_element = findChild(element, "MappingInformationType");
		const Element* reference_element = findChild(element, "ReferenceInformationType");

		if (mapping_element && mapping_element->first_property)
		{
			if (mapping_element->first_property->value == "ByPolygonVertex")
			{
				*mapping = GeometryImpl::BY_POLYGON_VERTEX;
			}
			else if (mapping_element->first_property->value == "ByPolygon")
			{
				*mapping = GeometryImpl::BY_POLYGON;
			}
			else if (mapping_element->first_property->value == "ByVertice" ||
				mapping_element->first_property->value == "ByVertex")
			{
				*mapping = GeometryImpl::BY_VERTEX;
			}
			else
			{
				return false;
			}
		}
		if (reference_element && reference_element->first_property)
		{
			if (reference_element->first_property->value == "IndexToDirect")
			{
				const Element* indices_element = findChild(element, index_name);
				if (indices_element && indices_element->first_property)
				{
					if (!parseBinaryArray(*indices_element->first_property, out_indices)) return false;
				}
			}
			else if (reference_element->first_property->value != "Direct")
			{
				return false;
			}
		}
		return parseDoubleVecData(*data_element->first_property, out);
	}

	const Element* findChild(const Element& element, const char* id);
	int getTriCountFromPoly(const std::vector<int>& indices, int* idx);

	OptionalError<Object*> parseGeometryForRendering(const Scene& scene, const Element& element);

} // namespace ofbx