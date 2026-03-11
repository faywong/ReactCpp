#include "runtime.hpp"

TypeId host_type_view() {
    static int dummy;
    return &dummy;
}

TypeId host_type_text() {
    static int dummy;
    return &dummy;
}

Element View(const std::vector<Element>& children) {
    Element e;
    e.type = host_type_view();
    e.children = children;
    return e;
}

Element Text(const std::string& text) {
    Element e;
    e.type = host_type_text();
    e.props.text = text;
    return e;
}

thread_local HookDispatcher g_dispatcher;

Runtime::Runtime(AppRenderFunc fn)
    : app_render(std::move(fn)) {}

InstanceNode* Runtime::create_instance(const Element& vnode, InstanceNode* parent) {
    auto inst = std::make_unique<InstanceNode>();
    inst->id = next_id++;
    inst->type = vnode.type;
    inst->current_vnode = vnode;
    inst->parent = parent;
    InstanceNode* raw = inst.get();
    if (parent) {
        parent->children.push_back(std::move(inst));
    } else {
        root_instance = std::move(inst);
    }
    return raw;
}

void Runtime::reconcile_children(InstanceNode* inst, const std::vector<Element>& new_children) {
    const std::size_t old_size = inst->children.size();
    const std::size_t new_size = new_children.size();
    const std::size_t common = old_size < new_size ? old_size : new_size;

    for (std::size_t i = 0; i < common; ++i) {
        reconcile(inst->children[i].get(), new_children[i]);
    }

    inst->children.resize(new_size);

    for (std::size_t i = common; i < new_size; ++i) {
        const Element& child_vnode = new_children[i];
        auto child_inst = std::make_unique<InstanceNode>();
        child_inst->id = next_id++;
        child_inst->type = child_vnode.type;
        child_inst->current_vnode = child_vnode;
        child_inst->parent = inst;
        inst->children[i] = std::move(child_inst);
    }
}

void Runtime::reconcile(InstanceNode* inst, const Element& vnode) {
    if (!inst) {
        return;
    }

    if (inst->type != vnode.type) {
        inst->type = vnode.type;
        inst->current_vnode = vnode;
        inst->children.clear();
        inst->hooks.clear();
    } else {
        inst->current_vnode = vnode;
    }

    reconcile_children(inst, vnode.children);
}

static void render_instance_to_console(const InstanceNode* inst, int indent = 0) {
    std::string pad(static_cast<std::size_t>(indent), ' ');

    if (inst->type == host_type_view()) {
        std::cout << pad << "View\n";
    } else if (inst->type == host_type_text()) {
        std::cout << pad << "Text: \"" << inst->current_vnode.props.text << "\"\n";
    } else {
        std::cout << pad << "Unknown node\n";
    }

    for (const auto& child : inst->children) {
        render_instance_to_console(child.get(), indent + 2);
    }
}

void Runtime::render_to_console() {
    if (!root_instance) {
        Element root_vnode = app_render();
        create_instance(root_vnode, nullptr);
    } else {
        Element root_vnode = app_render();
        reconcile(root_instance.get(), root_vnode);
    }

    std::cout << "----- Render Tree -----\n";
    render_instance_to_console(root_instance.get(), 0);
    std::cout << "-----------------------\n";
}

int run_app(const AppRenderFunc& app) {
    Runtime runtime(app);

    for (int frame = 0; frame < 3; ++frame) {
        std::cout << "\n=== Frame " << frame << " ===\n";

        runtime.render_to_console();
    }

    return 0;
}
