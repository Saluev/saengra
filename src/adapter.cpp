#define PY_SSIZE_T_CLEAN
#include <Python.h>
#include <structmember.h>
#include <string>
#include <vector>
#include <memory>
#include "absl/container/flat_hash_map.h"
#include "graph.h"
#include "observer_container.h"
#include "parser.h"
#include "matcher.h"
#include "start_positions.h"
#include "vertex.h"
#include "edge.h"

// ============================================================================
// PickleCache — reused from saengra_pyproto pattern
// ============================================================================

struct PickleCache {
    // Two-level cache: C++ map from type pointer to per-type Python dict.
    // Avoids PyTuple_Pack on every lookup; type pointers are stable (immortal).
    absl::flat_hash_map<PyTypeObject*, PyObject*> type_to_dict;
    PyObject* pickle_module = nullptr;
    PyObject* pickle_dumps = nullptr;
    PyObject* pickle_loads = nullptr;

    PickleCache() {
        pickle_module = PyImport_ImportModule("pickle");
        if (pickle_module) {
            pickle_dumps = PyObject_GetAttrString(pickle_module, "dumps");
            pickle_loads = PyObject_GetAttrString(pickle_module, "loads");
        }
    }

    ~PickleCache() {
        for (auto& [type, dict] : type_to_dict) {
            Py_XDECREF(dict);
        }
        Py_XDECREF(pickle_loads);
        Py_XDECREF(pickle_dumps);
        Py_XDECREF(pickle_module);
    }

    PyObject* get_type_dict(PyTypeObject* type) {
        auto it = type_to_dict.find(type);
        if (it != type_to_dict.end()) {
            return it->second;
        }
        PyObject* dict = PyDict_New();
        type_to_dict[type] = dict;
        return dict;
    }

    std::string get_pickled(PyObject* obj) {
        PyTypeObject* type = Py_TYPE(obj);
        PyObject* dict = get_type_dict(type);

        PyObject* cached = PyDict_GetItem(dict, obj);
        if (cached) {
            char* data;
            Py_ssize_t size;
            PyBytes_AsStringAndSize(cached, &data, &size);
            return std::string(data, size);
        }

        if (!pickle_dumps) {
            PyErr_SetString(PyExc_RuntimeError, "pickle.dumps not available");
            return "";
        }

        PyObject* result = PyObject_CallFunctionObjArgs(pickle_dumps, obj, NULL);
        if (!result) {
            return "";
        }

        if (!PyBytes_Check(result)) {
            Py_DECREF(result);
            PyErr_SetString(PyExc_TypeError, "pickle.dumps did not return bytes");
            return "";
        }

        char* data;
        Py_ssize_t size;
        PyBytes_AsStringAndSize(result, &data, &size);
        std::string pickled(data, size);

        PyDict_SetItem(dict, obj, result);
        Py_DECREF(result);

        return pickled;
    }

    PyObject* unpickle(const std::string& data) {
        if (!pickle_loads) {
            PyErr_SetString(PyExc_RuntimeError, "pickle.loads not available");
            return nullptr;
        }

        PyObject* bytes = PyBytes_FromStringAndSize(data.data(), data.size());
        if (!bytes) return nullptr;

        PyObject* result = PyObject_CallFunctionObjArgs(pickle_loads, bytes, NULL);
        Py_DECREF(bytes);
        return result;
    }

    void clear() {
        for (auto& [type, dict] : type_to_dict) {
            PyDict_Clear(dict);
        }
    }
};

// ============================================================================
// Helper: get type name of a Python object
// ============================================================================

static std::string get_type_name(PyObject* obj) {
    PyObject* type_obj = (PyObject*)Py_TYPE(obj);
    PyObject* type_name_obj = PyObject_GetAttrString(type_obj, "__name__");
    if (!type_name_obj) return "";
    const char* type_name = PyUnicode_AsUTF8(type_name_obj);
    std::string result(type_name ? type_name : "");
    Py_DECREF(type_name_obj);
    return result;
}

// ============================================================================
// DirectAdapter type
// ============================================================================

typedef struct {
    PyObject_HEAD
    saengra::Graph* graph;
    saengra::ObserverContainer* observer_container;
    PickleCache* pickle_cache;
    std::vector<saengra::NativeUpdate>* pending_updates;
    // Cached type pointers for fast dispatch in convert_one_update
    PyTypeObject* AddEdge_type;
    PyTypeObject* AddEdges_type;
    PyTypeObject* RemoveEdgesToAll_type;
    PyTypeObject* AddVertex_type;
    PyTypeObject* AddVertices_type;
    PyTypeObject* RemoveVertex_type;
    PyTypeObject* RemoveEdge_type;
    // Slot offsets for direct memory access (avoids PyObject_GetAttr entirely)
    Py_ssize_t off_AddEdge_from;
    Py_ssize_t off_AddEdge_label;
    Py_ssize_t off_AddEdge_to;
    Py_ssize_t off_AddEdges_from;
    Py_ssize_t off_AddEdges_label;
    Py_ssize_t off_AddEdges_tos;
    Py_ssize_t off_RemoveEdgesToAll_from;
    Py_ssize_t off_RemoveEdgesToAll_label;
    Py_ssize_t off_AddVertex_primitive;
    Py_ssize_t off_AddVertices_primitives;
    Py_ssize_t off_RemoveVertex_primitive;
    Py_ssize_t off_RemoveEdge_from;
    Py_ssize_t off_RemoveEdge_label;
    Py_ssize_t off_RemoveEdge_to;
} DirectAdapterObject;

// Read a PyObject* slot directly from an object at a given byte offset.
static inline PyObject* slot_get(PyObject* obj, Py_ssize_t offset) {
    return *(PyObject**)((char*)obj + offset);
}

// Find the byte offset of a named member in a type's tp_members table.
// Returns -1 if not found.
static Py_ssize_t find_member_offset(PyTypeObject* type, const char* name) {
    PyMemberDef* members = type->tp_members;
    if (!members) return -1;
    for (PyMemberDef* m = members; m->name != nullptr; m++) {
        if (strcmp(m->name, name) == 0) {
            return m->offset;
        }
    }
    return -1;
}

// Helper: convert a Python Primitive to (type_name, pickled_value)
static bool primitive_to_vertex_data(DirectAdapterObject* self, PyObject* obj,
                                      std::string& type_name, std::string& value) {
    type_name = get_type_name(obj);
    if (type_name.empty() && PyErr_Occurred()) return false;
    value = self->pickle_cache->get_pickled(obj);
    if (PyErr_Occurred()) return false;
    return true;
}

// Helper: convert a VertexID (StoredVertex*) back to Python Primitive
static PyObject* vertex_id_to_python(DirectAdapterObject* self, saengra::VertexID vertex_id) {
    return self->pickle_cache->unpickle(vertex_id->value);
}

// ============================================================================
// DirectAdapter methods
// ============================================================================

static void DirectAdapter_dealloc(DirectAdapterObject* self) {
    delete self->pending_updates;
    delete self->observer_container;
    delete self->graph;
    delete self->pickle_cache;
    Py_XDECREF(self->AddEdge_type);
    Py_XDECREF(self->AddEdges_type);
    Py_XDECREF(self->RemoveEdgesToAll_type);
    Py_XDECREF(self->AddVertex_type);
    Py_XDECREF(self->AddVertices_type);
    Py_XDECREF(self->RemoveVertex_type);
    Py_XDECREF(self->RemoveEdge_type);
    Py_TYPE(self)->tp_free((PyObject*)self);
}

static PyObject* DirectAdapter_new(PyTypeObject* type, PyObject* args, PyObject* kwds) {
    DirectAdapterObject* self = (DirectAdapterObject*)type->tp_alloc(type, 0);
    if (self != NULL) {
        self->graph = new saengra::Graph();
        self->observer_container = new saengra::ObserverContainer(*self->graph);
        self->pickle_cache = new PickleCache();
        self->pending_updates = new std::vector<saengra::NativeUpdate>();
    }
    return (PyObject*)self;
}

static int DirectAdapter_init(DirectAdapterObject* self, PyObject* args, PyObject* kwds) {
    // Cache update type pointers for fast dispatch (avoids get_type_name string comparison)
    PyObject* graph_module = PyImport_ImportModule("saengra.graph");
    if (!graph_module) return -1;

    self->AddEdge_type = (PyTypeObject*)PyObject_GetAttrString(graph_module, "AddEdge");
    self->AddEdges_type = (PyTypeObject*)PyObject_GetAttrString(graph_module, "AddEdges");
    self->RemoveEdgesToAll_type = (PyTypeObject*)PyObject_GetAttrString(graph_module, "RemoveEdgesToAll");
    self->AddVertex_type = (PyTypeObject*)PyObject_GetAttrString(graph_module, "AddVertex");
    self->AddVertices_type = (PyTypeObject*)PyObject_GetAttrString(graph_module, "AddVertices");
    self->RemoveVertex_type = (PyTypeObject*)PyObject_GetAttrString(graph_module, "RemoveVertex");
    self->RemoveEdge_type = (PyTypeObject*)PyObject_GetAttrString(graph_module, "RemoveEdge");
    Py_DECREF(graph_module);

    if (!self->AddEdge_type || !self->AddEdges_type || !self->RemoveEdgesToAll_type ||
        !self->AddVertex_type || !self->AddVertices_type ||
        !self->RemoveVertex_type || !self->RemoveEdge_type) {
        return -1;
    }

    // Look up slot offsets for direct memory access
    self->off_AddEdge_from = find_member_offset(self->AddEdge_type, "from_");
    self->off_AddEdge_label = find_member_offset(self->AddEdge_type, "label");
    self->off_AddEdge_to = find_member_offset(self->AddEdge_type, "to");
    self->off_AddEdges_from = find_member_offset(self->AddEdges_type, "from_");
    self->off_AddEdges_label = find_member_offset(self->AddEdges_type, "label");
    self->off_AddEdges_tos = find_member_offset(self->AddEdges_type, "tos");
    self->off_RemoveEdgesToAll_from = find_member_offset(self->RemoveEdgesToAll_type, "from_");
    self->off_RemoveEdgesToAll_label = find_member_offset(self->RemoveEdgesToAll_type, "label");
    self->off_AddVertex_primitive = find_member_offset(self->AddVertex_type, "primitive");
    self->off_AddVertices_primitives = find_member_offset(self->AddVertices_type, "primitives");
    self->off_RemoveVertex_primitive = find_member_offset(self->RemoveVertex_type, "primitive");
    self->off_RemoveEdge_from = find_member_offset(self->RemoveEdge_type, "from_");
    self->off_RemoveEdge_label = find_member_offset(self->RemoveEdge_type, "label");
    self->off_RemoveEdge_to = find_member_offset(self->RemoveEdge_type, "to");

    return 0;
}

// --- update(update_or_updates) ---

static bool convert_one_update(DirectAdapterObject* self, PyObject* update) {
    PyTypeObject* update_type = Py_TYPE(update);

    saengra::NativeUpdate native_update;

    if (update_type == self->AddEdge_type) {
        native_update.kind = saengra::NativeUpdateKind::AddEdge;

        PyObject* from_obj = slot_get(update, self->off_AddEdge_from);
        PyObject* label_obj = slot_get(update, self->off_AddEdge_label);
        PyObject* to_obj = slot_get(update, self->off_AddEdge_to);

        const char* label = PyUnicode_AsUTF8(label_obj);
        if (!label) return false;
        native_update.label = label;

        if (!primitive_to_vertex_data(self, from_obj, native_update.from.type_name, native_update.from.value) ||
            !primitive_to_vertex_data(self, to_obj, native_update.to.type_name, native_update.to.value)) {
            return false;
        }

    } else if (update_type == self->RemoveEdgesToAll_type) {
        native_update.kind = saengra::NativeUpdateKind::RemoveEdgesToAll;

        PyObject* from_obj = slot_get(update, self->off_RemoveEdgesToAll_from);
        PyObject* label_obj = slot_get(update, self->off_RemoveEdgesToAll_label);

        const char* label = PyUnicode_AsUTF8(label_obj);
        if (!label) return false;
        native_update.label = label;

        if (!primitive_to_vertex_data(self, from_obj, native_update.from.type_name, native_update.from.value)) {
            return false;
        }

    } else if (update_type == self->AddVertex_type) {
        native_update.kind = saengra::NativeUpdateKind::AddVertex;

        PyObject* primitive = slot_get(update, self->off_AddVertex_primitive);
        if (!primitive_to_vertex_data(self, primitive, native_update.from.type_name, native_update.from.value)) {
            return false;
        }

    } else if (update_type == self->AddEdges_type) {
        native_update.kind = saengra::NativeUpdateKind::AddEdges;

        PyObject* from_obj = slot_get(update, self->off_AddEdges_from);
        PyObject* label_obj = slot_get(update, self->off_AddEdges_label);
        PyObject* tos_obj = slot_get(update, self->off_AddEdges_tos);

        const char* label = PyUnicode_AsUTF8(label_obj);
        if (!label) return false;
        native_update.label = label;

        if (!primitive_to_vertex_data(self, from_obj, native_update.from.type_name, native_update.from.value)) {
            return false;
        }

        Py_ssize_t n = PySequence_Length(tos_obj);
        if (n < 0) return false;
        native_update.tos.reserve(n);
        for (Py_ssize_t i = 0; i < n; i++) {
            PyObject* to = PySequence_GetItem(tos_obj, i);
            if (!to) return false;
            saengra::NativeUpdateVertex v;
            bool ok = primitive_to_vertex_data(self, to, v.type_name, v.value);
            Py_DECREF(to);
            if (!ok) return false;
            native_update.tos.push_back(std::move(v));
        }

    } else if (update_type == self->RemoveVertex_type) {
        native_update.kind = saengra::NativeUpdateKind::RemoveVertex;

        PyObject* primitive = slot_get(update, self->off_RemoveVertex_primitive);
        if (!primitive_to_vertex_data(self, primitive, native_update.from.type_name, native_update.from.value)) {
            return false;
        }

    } else if (update_type == self->RemoveEdge_type) {
        native_update.kind = saengra::NativeUpdateKind::RemoveEdge;

        PyObject* from_obj = slot_get(update, self->off_RemoveEdge_from);
        PyObject* label_obj = slot_get(update, self->off_RemoveEdge_label);
        PyObject* to_obj = slot_get(update, self->off_RemoveEdge_to);

        const char* label = PyUnicode_AsUTF8(label_obj);
        if (!label) return false;
        native_update.label = label;

        if (!primitive_to_vertex_data(self, from_obj, native_update.from.type_name, native_update.from.value) ||
            !primitive_to_vertex_data(self, to_obj, native_update.to.type_name, native_update.to.value)) {
            return false;
        }

    } else if (update_type == self->AddVertices_type) {
        native_update.kind = saengra::NativeUpdateKind::AddVertices;

        PyObject* primitives_obj = slot_get(update, self->off_AddVertices_primitives);
        Py_ssize_t n = PySequence_Length(primitives_obj);
        if (n < 0) return false;
        native_update.vertices.reserve(n);
        for (Py_ssize_t i = 0; i < n; i++) {
            PyObject* prim = PySequence_GetItem(primitives_obj, i);
            if (!prim) return false;
            saengra::NativeUpdateVertex v;
            bool ok = primitive_to_vertex_data(self, prim, v.type_name, v.value);
            Py_DECREF(prim);
            if (!ok) return false;
            native_update.vertices.push_back(std::move(v));
        }

    } else {
        PyErr_Format(PyExc_TypeError, "not an update: %s", update_type->tp_name);
        return false;
    }

    self->pending_updates->push_back(std::move(native_update));
    return true;
}

static PyObject* DirectAdapter_update(DirectAdapterObject* self, PyObject* args) {
    PyObject* update_or_updates;
    if (!PyArg_ParseTuple(args, "O", &update_or_updates)) return NULL;

    if (PyList_Check(update_or_updates) || PyTuple_Check(update_or_updates)) {
        PyObject* iter = PyObject_GetIter(update_or_updates);
        if (!iter) return NULL;
        PyObject* item;
        while ((item = PyIter_Next(iter))) {
            if (!convert_one_update(self, item)) {
                Py_DECREF(item);
                Py_DECREF(iter);
                return NULL;
            }
            Py_DECREF(item);
        }
        Py_DECREF(iter);
        if (PyErr_Occurred()) return NULL;
    } else {
        if (!convert_one_update(self, update_or_updates)) return NULL;
    }

    Py_RETURN_NONE;
}

// --- flush() ---

static PyObject* DirectAdapter_flush(DirectAdapterObject* self, PyObject* Py_UNUSED(ignored)) {
    if (self->pending_updates->empty()) {
        Py_RETURN_NONE;
    }
    self->graph->update(*self->pending_updates);
    self->pending_updates->clear();
    Py_RETURN_NONE;
}

// --- commit() ---

static PyObject* DirectAdapter_commit(DirectAdapterObject* self, PyObject* Py_UNUSED(ignored)) {
    // flush first
    if (!self->pending_updates->empty()) {
        self->graph->update(*self->pending_updates);
        self->pending_updates->clear();
    }

    auto graph_observations = self->graph->apply();
    if (graph_observations.empty()) {
        self->graph->commit();
        // return (False, [])
        PyObject* empty_list = PyList_New(0);
        PyObject* result = PyTuple_Pack(2, Py_False, empty_list);
        Py_DECREF(empty_list);
        return result;
    }

    auto native_obs = self->observer_container->observe_native(graph_observations);
    if (native_obs.empty()) {
        self->graph->commit();
        PyObject* empty_list = PyList_New(0);
        PyObject* result = PyTuple_Pack(2, Py_False, empty_list);
        Py_DECREF(empty_list);
        return result;
    }

    // Import Python types for observations
    PyObject* api_module = PyImport_ImportModule("saengra.api");
    if (!api_module) return NULL;

    PyObject* Observation_class = PyObject_GetAttrString(api_module, "Observation");
    PyObject* ObservationType_class = PyObject_GetAttrString(api_module, "ObservationType");
    Py_DECREF(api_module);

    if (!Observation_class || !ObservationType_class) {
        Py_XDECREF(Observation_class);
        Py_XDECREF(ObservationType_class);
        return NULL;
    }

    PyObject* obs_list = PyList_New(0);
    for (const auto& obs : native_obs) {
        // Build variables dict: ref_name -> unpickled Primitive
        PyObject* variables = PyDict_New();
        for (const auto& [ref_name, vertex_id] : obs.variables) {
            PyObject* key = PyUnicode_FromStringAndSize(ref_name.data(), ref_name.size());
            PyObject* value = vertex_id_to_python(self, vertex_id);
            if (!key || !value) {
                Py_XDECREF(key); Py_XDECREF(value);
                Py_DECREF(variables); Py_DECREF(obs_list);
                Py_DECREF(Observation_class); Py_DECREF(ObservationType_class);
                return NULL;
            }
            PyDict_SetItem(variables, key, value);
            Py_DECREF(key); Py_DECREF(value);
        }

        // Map NativeObservationType to protobuf int value for ObservationType enum
        int obs_type_value;
        switch (obs.type) {
            case saengra::NativeObservationType::ON_CREATE: obs_type_value = 0; break;
            case saengra::NativeObservationType::ON_CHANGE: obs_type_value = 1; break;
            case saengra::NativeObservationType::ON_DELETE: obs_type_value = 2; break;
        }

        PyObject* obs_type = PyObject_CallFunction(ObservationType_class, "i", obs_type_value);
        PyObject* observation = PyObject_CallFunction(Observation_class, "sOO",
            obs.observer_id.c_str(), obs_type, variables);

        Py_DECREF(obs_type); Py_DECREF(variables);

        if (!observation) {
            Py_DECREF(obs_list);
            Py_DECREF(Observation_class); Py_DECREF(ObservationType_class);
            return NULL;
        }

        PyList_Append(obs_list, observation);
        Py_DECREF(observation);
    }

    Py_DECREF(Observation_class); Py_DECREF(ObservationType_class);

    // return (True, observations)
    PyObject* result = PyTuple_Pack(2, Py_True, obs_list);
    Py_DECREF(obs_list);
    return result;
}

// --- rollback() ---

static PyObject* DirectAdapter_rollback(DirectAdapterObject* self, PyObject* Py_UNUSED(ignored)) {
    self->graph->rollback();
    self->observer_container->reinitialize_all();
    self->pending_updates->clear();
    Py_RETURN_NONE;
}

// --- find_vertices(with_type_name=None) ---

static PyObject* DirectAdapter_find_vertices(DirectAdapterObject* self, PyObject* args, PyObject* kwds) {
    static const char* kwlist[] = {"with_type_name", NULL};
    const char* with_type_name = NULL;

    if (!PyArg_ParseTupleAndKeywords(args, kwds, "|z", const_cast<char**>(kwlist), &with_type_name))
        return NULL;

    // flush first
    if (!self->pending_updates->empty()) {
        self->graph->update(*self->pending_updates);
        self->pending_updates->clear();
    }

    const auto& vertices = self->graph->get_vertices();

    saengra::VertexIDRange vertex_ids = with_type_name
        ? vertices.iter_present_by_type_name(self->graph->internalize_type_name(with_type_name))
        : vertices.iter_present();

    PyObject* result = PyList_New(0);
    for (saengra::VertexID vertex_id : vertex_ids) {
        PyObject* py_vertex = vertex_id_to_python(self, vertex_id);
        if (!py_vertex) {
            Py_DECREF(result);
            return NULL;
        }
        PyList_Append(result, py_vertex);
        Py_DECREF(py_vertex);
    }

    return result;
}

// --- find_edges(with_label=None, from_vertices=None, to_vertices=None) ---

static PyObject* DirectAdapter_find_edges(DirectAdapterObject* self, PyObject* args, PyObject* kwds) {
    static const char* kwlist[] = {"with_label", "from_vertices", "to_vertices", NULL};
    const char* with_label = NULL;
    PyObject* from_vertices_obj = NULL;
    PyObject* to_vertices_obj = NULL;

    if (!PyArg_ParseTupleAndKeywords(args, kwds, "|zOO", const_cast<char**>(kwlist),
                                      &with_label, &from_vertices_obj, &to_vertices_obj))
        return NULL;

    // flush first
    if (!self->pending_updates->empty()) {
        self->graph->update(*self->pending_updates);
        self->pending_updates->clear();
    }

    const auto& vertices_container = self->graph->get_vertices();
    const auto& edges_container = self->graph->get_edges();

    // Convert from_vertices to VertexIDs
    std::vector<saengra::VertexID> from_ids;
    bool has_from = from_vertices_obj && from_vertices_obj != Py_None && PySequence_Length(from_vertices_obj) > 0;
    if (has_from) {
        Py_ssize_t n = PySequence_Length(from_vertices_obj);
        for (Py_ssize_t i = 0; i < n; i++) {
            PyObject* item = PySequence_GetItem(from_vertices_obj, i);
            if (!item) return NULL;
            std::string tn, val;
            if (!primitive_to_vertex_data(self, item, tn, val)) {
                Py_DECREF(item);
                return NULL;
            }
            Py_DECREF(item);
            auto type_name = self->graph->internalize_type_name(tn);
            auto vid = vertices_container.get_vertex_id(saengra::VertexData(type_name, val));
            if (vid.has_value()) from_ids.push_back(vid.value());
        }
    }

    // Convert to_vertices to VertexIDs
    std::vector<saengra::VertexID> to_ids;
    bool has_to = to_vertices_obj && to_vertices_obj != Py_None && PySequence_Length(to_vertices_obj) > 0;
    if (has_to) {
        Py_ssize_t n = PySequence_Length(to_vertices_obj);
        for (Py_ssize_t i = 0; i < n; i++) {
            PyObject* item = PySequence_GetItem(to_vertices_obj, i);
            if (!item) return NULL;
            std::string tn, val;
            if (!primitive_to_vertex_data(self, item, tn, val)) {
                Py_DECREF(item);
                return NULL;
            }
            Py_DECREF(item);
            auto type_name = self->graph->internalize_type_name(tn);
            auto vid = vertices_container.get_vertex_id(saengra::VertexData(type_name, val));
            if (vid.has_value()) to_ids.push_back(vid.value());
        }
    }

    std::vector<saengra::Edge> edges;

    if (has_from && has_to) {
        // Query from "from" vertices, then filter by "to"
        std::unordered_set<saengra::VertexID> to_set(to_ids.begin(), to_ids.end());
        for (auto from_id : from_ids) {
            std::vector<saengra::Edge> from_edges;
            if (with_label) {
                auto label = self->graph->internalize_label(with_label);
                from_edges = edges_container.iter_present_from_vertex_via_labels(from_id, {label});
            } else {
                from_edges = edges_container.iter_present_from_vertex(from_id);
            }
            for (auto& e : from_edges) {
                if (to_set.count(e.to)) {
                    edges.push_back(e);
                }
            }
        }
    } else if (has_from) {
        for (auto from_id : from_ids) {
            std::vector<saengra::Edge> from_edges;
            if (with_label) {
                auto label = self->graph->internalize_label(with_label);
                from_edges = edges_container.iter_present_from_vertex_via_labels(from_id, {label});
            } else {
                from_edges = edges_container.iter_present_from_vertex(from_id);
            }
            edges.insert(edges.end(), from_edges.begin(), from_edges.end());
        }
    } else if (has_to) {
        for (auto to_id : to_ids) {
            std::vector<saengra::Edge> to_edges;
            if (with_label) {
                auto label = self->graph->internalize_label(with_label);
                to_edges = edges_container.iter_present_to_vertex_via_labels(to_id, {label});
            } else {
                to_edges = edges_container.iter_present_to_vertex(to_id);
            }
            edges.insert(edges.end(), to_edges.begin(), to_edges.end());
        }
    } else {
        if (with_label) {
            auto label = self->graph->internalize_label(with_label);
            edges = edges_container.iter_present_via_labels({label});
        } else {
            edges = edges_container.iter_present();
        }
    }

    // Import Edge class
    PyObject* graph_module = PyImport_ImportModule("saengra.graph");
    if (!graph_module) return NULL;
    PyObject* Edge_class = PyObject_GetAttrString(graph_module, "Edge");
    Py_DECREF(graph_module);
    if (!Edge_class) return NULL;

    PyObject* result = PyList_New(0);
    for (const auto& edge : edges) {
        PyObject* from_v = vertex_id_to_python(self, edge.from);
        PyObject* label = PyUnicode_FromString(edge.label.text->c_str());
        PyObject* to_v = vertex_id_to_python(self, edge.to);

        if (!from_v || !label || !to_v) {
            Py_XDECREF(from_v); Py_XDECREF(label); Py_XDECREF(to_v);
            Py_DECREF(result); Py_DECREF(Edge_class);
            return NULL;
        }

        PyObject* py_edge = PyObject_CallFunction(Edge_class, "OOO", from_v, label, to_v);
        Py_DECREF(from_v); Py_DECREF(label); Py_DECREF(to_v);

        if (!py_edge) {
            Py_DECREF(result); Py_DECREF(Edge_class);
            return NULL;
        }

        PyList_Append(result, py_edge);
        Py_DECREF(py_edge);
    }

    Py_DECREF(Edge_class);
    return result;
}

// --- find_all() ---

static PyObject* DirectAdapter_find_all(DirectAdapterObject* self, PyObject* Py_UNUSED(ignored)) {
    // flush first
    if (!self->pending_updates->empty()) {
        self->graph->update(*self->pending_updates);
        self->pending_updates->clear();
    }

    // Get vertices
    PyObject* vertices_args = PyTuple_New(0);
    PyObject* vertices_kwds = PyDict_New();
    PyObject* vertices_list = DirectAdapter_find_vertices(self, vertices_args, vertices_kwds);
    Py_DECREF(vertices_args);
    Py_DECREF(vertices_kwds);
    if (!vertices_list) return NULL;

    // Get edges
    PyObject* edges_args = PyTuple_New(0);
    PyObject* edges_kwds = PyDict_New();
    PyObject* edges_list = DirectAdapter_find_edges(self, edges_args, edges_kwds);
    Py_DECREF(edges_args);
    Py_DECREF(edges_kwds);
    if (!edges_list) {
        Py_DECREF(vertices_list);
        return NULL;
    }

    PyObject* result = PyTuple_Pack(2, vertices_list, edges_list);
    Py_DECREF(vertices_list);
    Py_DECREF(edges_list);
    return result;
}

// --- observe(observers) ---

static PyObject* DirectAdapter_observe(DirectAdapterObject* self, PyObject* args) {
    PyObject* observers_obj;
    if (!PyArg_ParseTuple(args, "O", &observers_obj)) return NULL;

    // flush first
    if (!self->pending_updates->empty()) {
        self->graph->update(*self->pending_updates);
        self->pending_updates->clear();
    }

    PyObject* iter = PyObject_GetIter(observers_obj);
    if (!iter) return NULL;

    PyObject* observer;
    while ((observer = PyIter_Next(iter))) {
        PyObject* id_obj = PyObject_GetAttrString(observer, "id");
        PyObject* expression_obj = PyObject_GetAttrString(observer, "expression");
        PyObject* placeholder_values_obj = PyObject_GetAttrString(observer, "placeholder_values");
        PyObject* on_create_obj = PyObject_GetAttrString(observer, "on_create");
        PyObject* on_change_obj = PyObject_GetAttrString(observer, "on_change");
        PyObject* on_delete_obj = PyObject_GetAttrString(observer, "on_delete");

        if (!id_obj || !expression_obj || !placeholder_values_obj ||
            !on_create_obj || !on_change_obj || !on_delete_obj) {
            Py_XDECREF(id_obj); Py_XDECREF(expression_obj); Py_XDECREF(placeholder_values_obj);
            Py_XDECREF(on_create_obj); Py_XDECREF(on_change_obj); Py_XDECREF(on_delete_obj);
            Py_DECREF(observer); Py_DECREF(iter);
            return NULL;
        }

        const char* id = PyUnicode_AsUTF8(id_obj);
        const char* expression = PyUnicode_AsUTF8(expression_obj);
        if (!id || !expression) {
            Py_DECREF(id_obj); Py_DECREF(expression_obj); Py_DECREF(placeholder_values_obj);
            Py_DECREF(on_create_obj); Py_DECREF(on_change_obj); Py_DECREF(on_delete_obj);
            Py_DECREF(observer); Py_DECREF(iter);
            return NULL;
        }

        // Convert placeholder values
        saengra::PlaceholderValues placeholder_values;
        PyObject* pv_iter = PyObject_GetIter(placeholder_values_obj);
        if (pv_iter) {
            PyObject* pv_item;
            while ((pv_item = PyIter_Next(pv_iter))) {
                std::string tn, val;
                if (!primitive_to_vertex_data(self, pv_item, tn, val)) {
                    Py_DECREF(pv_item); Py_DECREF(pv_iter);
                    Py_DECREF(id_obj); Py_DECREF(expression_obj); Py_DECREF(placeholder_values_obj);
                    Py_DECREF(on_create_obj); Py_DECREF(on_change_obj); Py_DECREF(on_delete_obj);
                    Py_DECREF(observer); Py_DECREF(iter);
                    return NULL;
                }
                auto type_name = self->graph->internalize_type_name(tn);
                placeholder_values.emplace_back(type_name, val);
                Py_DECREF(pv_item);
            }
            Py_DECREF(pv_iter);
        }

        bool on_create = PyObject_IsTrue(on_create_obj);
        bool on_change = PyObject_IsTrue(on_change_obj);
        bool on_delete = PyObject_IsTrue(on_delete_obj);

        try {
            self->observer_container->register_native(
                id, expression, placeholder_values,
                on_create, on_change, on_delete
            );
        } catch (const std::exception& e) {
            PyErr_SetString(PyExc_RuntimeError, e.what());
            Py_DECREF(id_obj); Py_DECREF(expression_obj); Py_DECREF(placeholder_values_obj);
            Py_DECREF(on_create_obj); Py_DECREF(on_change_obj); Py_DECREF(on_delete_obj);
            Py_DECREF(observer); Py_DECREF(iter);
            return NULL;
        }

        Py_DECREF(id_obj); Py_DECREF(expression_obj); Py_DECREF(placeholder_values_obj);
        Py_DECREF(on_create_obj); Py_DECREF(on_change_obj); Py_DECREF(on_delete_obj);
        Py_DECREF(observer);
    }
    Py_DECREF(iter);
    if (PyErr_Occurred()) return NULL;

    Py_RETURN_NONE;
}

// --- match(expression, *placeholder_values) ---

static PyObject* DirectAdapter_match(DirectAdapterObject* self, PyObject* args) {
    Py_ssize_t nargs = PyTuple_Size(args);
    if (nargs < 1) {
        PyErr_SetString(PyExc_TypeError, "match() requires at least 1 argument");
        return NULL;
    }

    PyObject* expression_obj = PyTuple_GetItem(args, 0);
    const char* expression = PyUnicode_AsUTF8(expression_obj);
    if (!expression) return NULL;

    // flush first
    if (!self->pending_updates->empty()) {
        self->graph->update(*self->pending_updates);
        self->pending_updates->clear();
    }

    // Convert placeholder values
    saengra::PlaceholderValues placeholder_values;
    for (Py_ssize_t i = 1; i < nargs; i++) {
        PyObject* pv = PyTuple_GetItem(args, i);
        std::string tn, val;
        if (!primitive_to_vertex_data(self, pv, tn, val)) return NULL;
        auto type_name = self->graph->internalize_type_name(tn);
        placeholder_values.emplace_back(type_name, val);
    }

    // Parse expression
    saengra::Parser parser(*self->graph);
    saengra::Expression expr = parser.parse(expression);

    // Create query
    saengra::Query query{*self->graph, expr, placeholder_values};

    // Find start positions
    saengra::StartPositionFinder finder;
    saengra::StartPositions start_positions = finder.find_start_positions(query);

    // Match
    saengra::Matcher matcher;
    saengra::QuerySet result = matcher.match(query, start_positions);

    // Import Python types
    PyObject* graph_module = PyImport_ImportModule("saengra.graph");
    if (!graph_module) return NULL;

    PyObject* Subgraph_class = PyObject_GetAttrString(graph_module, "Subgraph");
    PyObject* Position_class = PyObject_GetAttrString(graph_module, "Position");
    PyObject* Edge_class = PyObject_GetAttrString(graph_module, "Edge");
    PyObject* Refs_class = PyObject_GetAttrString(graph_module, "Refs");
    Py_DECREF(graph_module);

    if (!Subgraph_class || !Position_class || !Edge_class || !Refs_class) {
        Py_XDECREF(Subgraph_class); Py_XDECREF(Position_class);
        Py_XDECREF(Edge_class); Py_XDECREF(Refs_class);
        return NULL;
    }

    PyObject* subgraphs_list = PyList_New(0);
    for (const auto& sg : result.subgraphs) {
        // Build start position
        PyObject* start_vertex = vertex_id_to_python(self, sg.start_position.vertex_id);
        const char* start_point = sg.start_position.kind == saengra::PositionKind::CORE ? "." : "o";
        PyObject* start_pos = PyObject_CallFunction(Position_class, "Os", start_vertex, start_point);
        Py_XDECREF(start_vertex);
        if (!start_pos) goto match_error;

        {
            // Build end positions
            PyObject* end_positions = PyFrozenSet_New(NULL);
            for (const auto& pos : sg.end_positions) {
                PyObject* end_vertex = vertex_id_to_python(self, pos.vertex_id);
                const char* end_point = pos.kind == saengra::PositionKind::CORE ? "." : "o";
                PyObject* end_pos = PyObject_CallFunction(Position_class, "Os", end_vertex, end_point);
                Py_XDECREF(end_vertex);
                if (!end_pos) {
                    Py_DECREF(end_positions); Py_DECREF(start_pos);
                    goto match_error;
                }
                PySet_Add(end_positions, end_pos);
                Py_DECREF(end_pos);
            }

            // Build vertices
            PyObject* vertices = PyFrozenSet_New(NULL);
            for (auto vid : sg.vertices) {
                PyObject* v = vertex_id_to_python(self, vid);
                if (!v) {
                    Py_DECREF(vertices); Py_DECREF(end_positions); Py_DECREF(start_pos);
                    goto match_error;
                }
                PySet_Add(vertices, v);
                Py_DECREF(v);
            }

            // Build edges
            PyObject* edges = PyFrozenSet_New(NULL);
            for (const auto& e : sg.edges) {
                PyObject* from_v = vertex_id_to_python(self, e.from);
                PyObject* label = PyUnicode_FromString(e.label.text->c_str());
                PyObject* to_v = vertex_id_to_python(self, e.to);
                if (!from_v || !label || !to_v) {
                    Py_XDECREF(from_v); Py_XDECREF(label); Py_XDECREF(to_v);
                    Py_DECREF(edges); Py_DECREF(vertices);
                    Py_DECREF(end_positions); Py_DECREF(start_pos);
                    goto match_error;
                }
                PyObject* py_edge = PyObject_CallFunction(Edge_class, "OOO", from_v, label, to_v);
                Py_DECREF(from_v); Py_DECREF(label); Py_DECREF(to_v);
                if (!py_edge) {
                    Py_DECREF(edges); Py_DECREF(vertices);
                    Py_DECREF(end_positions); Py_DECREF(start_pos);
                    goto match_error;
                }
                PySet_Add(edges, py_edge);
                Py_DECREF(py_edge);
            }

            // Build refs
            PyObject* refs_dict = PyDict_New();
            for (const auto& [name, vid] : sg.refs) {
                PyObject* key = PyUnicode_FromStringAndSize(name.data(), name.size());
                PyObject* value = vertex_id_to_python(self, vid);
                if (!key || !value) {
                    Py_XDECREF(key); Py_XDECREF(value);
                    Py_DECREF(refs_dict); Py_DECREF(edges); Py_DECREF(vertices);
                    Py_DECREF(end_positions); Py_DECREF(start_pos);
                    goto match_error;
                }
                PyDict_SetItem(refs_dict, key, value);
                Py_DECREF(key); Py_DECREF(value);
            }
            PyObject* refs = PyObject_CallFunction(Refs_class, "O", refs_dict);
            Py_DECREF(refs_dict);

            // Create Subgraph
            PyObject* subgraph = PyObject_CallFunction(Subgraph_class, "OOOOO",
                start_pos, end_positions, vertices, edges, refs);
            Py_DECREF(start_pos); Py_DECREF(end_positions);
            Py_DECREF(vertices); Py_DECREF(edges); Py_DECREF(refs);

            if (!subgraph) goto match_error;

            PyList_Append(subgraphs_list, subgraph);
            Py_DECREF(subgraph);
        }
    }

    Py_DECREF(Subgraph_class); Py_DECREF(Position_class);
    Py_DECREF(Edge_class); Py_DECREF(Refs_class);
    return subgraphs_list;

match_error:
    Py_DECREF(subgraphs_list);
    Py_DECREF(Subgraph_class); Py_DECREF(Position_class);
    Py_DECREF(Edge_class); Py_DECREF(Refs_class);
    return NULL;
}

// ============================================================================
// Method table and type definition
// ============================================================================

static PyMethodDef DirectAdapter_methods[] = {
    {"update", (PyCFunction)DirectAdapter_update, METH_VARARGS,
     "Add update(s) to pending buffer"},
    {"flush", (PyCFunction)DirectAdapter_flush, METH_NOARGS,
     "Flush pending updates to the graph"},
    {"commit", (PyCFunction)DirectAdapter_commit, METH_NOARGS,
     "Commit pending changes, return (should_commit_again, observations)"},
    {"rollback", (PyCFunction)DirectAdapter_rollback, METH_NOARGS,
     "Rollback all uncommitted changes"},
    {"find_vertices", (PyCFunction)DirectAdapter_find_vertices, METH_VARARGS | METH_KEYWORDS,
     "Find vertices, optionally filtered by type name"},
    {"find_edges", (PyCFunction)DirectAdapter_find_edges, METH_VARARGS | METH_KEYWORDS,
     "Find edges with optional label/from/to filters"},
    {"find_all", (PyCFunction)DirectAdapter_find_all, METH_NOARGS,
     "Find all vertices and edges, return (vertices, edges)"},
    {"observe", (PyCFunction)DirectAdapter_observe, METH_VARARGS,
     "Register observers"},
    {"match", (PyCFunction)DirectAdapter_match, METH_VARARGS,
     "Match expression against graph, return list of Subgraph"},
    {NULL, NULL, 0, NULL}
};

static PyTypeObject DirectAdapterType = {
    .ob_base = PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "saengra.c_extension.DirectAdapter",
    .tp_basicsize = sizeof(DirectAdapterObject),
    .tp_itemsize = 0,
    .tp_dealloc = (destructor)DirectAdapter_dealloc,
    .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE,
    .tp_doc = "In-process adapter for saengra graph engine",
    .tp_methods = DirectAdapter_methods,
    .tp_init = (initproc)DirectAdapter_init,
    .tp_new = DirectAdapter_new,
};

static PyModuleDef adapter_module = {
    .m_base = PyModuleDef_HEAD_INIT,
    .m_name = "saengra.c_extension",
    .m_doc = "Native C++ extension for saengra graph engine",
    .m_size = -1,
};

PyMODINIT_FUNC PyInit_c_extension(void) {
    PyObject* m;

    if (PyType_Ready(&DirectAdapterType) < 0) {
        return NULL;
    }

    m = PyModule_Create(&adapter_module);
    if (m == NULL) {
        return NULL;
    }

    Py_INCREF(&DirectAdapterType);
    if (PyModule_AddObject(m, "DirectAdapter", (PyObject*)&DirectAdapterType) < 0) {
        Py_DECREF(&DirectAdapterType);
        Py_DECREF(m);
        return NULL;
    }

    return m;
}
