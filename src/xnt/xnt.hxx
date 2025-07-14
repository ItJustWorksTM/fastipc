#pragma once

namespace fastipc::xnt {

template <class T>
concept CrossNodeTransport = requires(T t) {
    t.connect_remote();
    t.register_channel();
    t.unregister_channel();
};

}
