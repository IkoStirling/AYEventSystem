# AYEventSystem Design

## 1. 概述

AYEventSystem 是 AY Engine 的**事件中枢**，负责：
- 模块间解耦通信
- 类型安全的事件订阅/发布
- 高效事件分发（同步/延迟队列）
- 事件对象池管理（零堆分配）
- 优先级分层分发

### 1.1 设计目标

- **类型安全**：模板化订阅，编译期类型检查，无字符串查找
- **零分配**：事件对象池复用，热路径无堆分配
- **延迟队列**：支持帧尾批量处理，避免重复分发
- **优先级分发**：4层优先级，输入/物理 > 渲染/UI > 游戏逻辑 > 后台
- **单总线**：EventBus 单一实例，全局事件中枢

### 1.2 在引擎中的位置

```
┌─────────────────────────────────────────────────────────────────┐
│                        Game Engine                               │
├─────────────────────────────────────────────────────────────────┤
│                                                                  │
│  ┌─────────┐  ┌─────────┐  ┌─────────┐  ┌─────────┐  ┌─────────┐│
│  │AYDevice │  │AYRenderer│  │AYUI    │  │AYResource│  │AYAudio  ││
│  │ (输入)  │  │ (渲染)  │  │ (UI)   │  │ (资源)  │  │ (音频)  ││
│  └────┬────┘  └────┬────┘  └────┬────┘  └────┬────┘  └────┬────┘│
│       │            │            │            │            │      │
│       └────────────┴────────────┴────────────┴────────────┘      │
│                                    │                            │
│                         ┌──────────▼──────────┐                │
│                         │     EventBus       │                │
│                         │   (事件中枢)       │                │
│                         └────────────────────┘                │
└─────────────────────────────────────────────────────────────────┘
```

---

## 2. 核心架构

### 2.1 设计决策

| 问题 | 决策 | 理由 |
|------|------|------|
| 事件类型 | Struct + 基类 | 值语义，栈分配，缓存友好 |
| 订阅机制 | 模板 + 类型ID | 编译期类型安全，无字符串查找 |
| 分发模式 | 同步 + 延迟队列 | 支持即时分发和帧尾批量处理 |
| 内存管理 | 对象池 | 避免每帧分配/释放，热路径零堆分配 |
| 优先级 | 4层优先级 | 输入/物理 > 渲染/UI > 游戏逻辑 > 后台 |

### 2.2 架构图

```
┌─────────────────────────────────────────────────────────────┐
│                        EventBus                              │
├─────────────────────────────────────────────────────────────┤
│                                                              │
│  ┌─────────────────────────────────────────────────────┐   │
│  │              EventPool (对象池)                      │   │
│  │  ┌─────┐ ┌─────┐ ┌─────┐ ┌─────┐ ┌─────┐            │   │
│  │  │ E1  │ │ E2  │ │ E3  │ │ E4  │ │ ... │            │   │
│  │  └─────┘ └─────┘ └─────┘ └─────┘ └─────┘            │   │
│  │  热路径零堆分配                                      │   │
│  └─────────────────────────────────────────────────────┘   │
│                                                              │
│  ┌─────────────────────────────────────────────────────┐   │
│  │            ListenerMap (类型 → 监听器)                │   │
│  │                                                      │   │
│  │  typeId(T) ──────► std::vector<Listener<T>>         │   │
│  │                                                      │   │
│  │  InputActionEvent ──► [listener1, listener2]        │   │
│  │  WindowResizeEvent  ──► [listener3]                  │   │
│  │                                                      │   │
│  └─────────────────────────────────────────────────────┘   │
│                                                              │
│  ┌─────────────────────────────────────────────────────┐   │
│  │         PriorityQueue (优先级队列)                    │   │
│  │                                                      │   │
│  │  Immediate: [event1, event2]                         │   │
│  │  High:     [event3]                                 │   │
│  │  Normal:   [event4, event5, event6]                 │   │
│  │  Low:      [event7]                                 │   │
│  │                                                      │   │
│  └─────────────────────────────────────────────────────┘   │
│                                                              │
│  ┌─────────────────────────────────────────────────────┐   │
│  │         EventQueue (延迟队列)                        │   │
│  │                                                      │   │
│  │  [eventA] → [eventB] → [eventC] → ...               │   │
│  │  帧尾批量分发                                        │   │
│  └─────────────────────────────────────────────────────┘   │
│                                                              │
└─────────────────────────────────────────────────────────────┘
```

---

## 3. 事件定义

### 3.1 事件基类

```cpp
// 最小基类，仅包含类型信息和优先级
struct IEvent {
    virtual ~IEvent() = default;
    virtual uint32_t typeId() const = 0;
    virtual EventPriority priority() const = 0;  // 默认优先级
};

// 事件优先级枚举
enum class EventPriority : uint8_t {
    Immediate = 0,  // 物理/输入（必须立即处理）
    High = 1,       // 渲染/UI
    Normal = 2,     // 游戏逻辑
    Low = 3,        // 资源加载/后台任务
};

// 辅助模板，记录静态类型ID
template<typename T>
struct EventTypeId {
    static uint32_t id() {
        static char c = 0;
        return reinterpret_cast<uint32_t>(&c);
    }
};
```

### 3.2 事件定义示例

```cpp
// 输入事件（Immediate 优先级）
struct InputActionEvent : IEvent {
    static constexpr uint32_t kTypeId = EventTypeId<InputActionEvent>::id();
    static constexpr EventPriority kPriority = EventPriority::Immediate;

    std::string action;
    bool pressed;
    float analogValue = 1.0f;

    uint32_t typeId() const override { return kTypeId; }
    EventPriority priority() const override { return kPriority; }
};

struct InputMouseMoveEvent : IEvent {
    static constexpr uint32_t kTypeId = EventTypeId<InputMouseMoveEvent>::id();
    static constexpr EventPriority kPriority = EventPriority::Immediate;

    float x, y;
    float deltaX, deltaY;

    uint32_t typeId() const override { return kTypeId; }
    EventPriority priority() const override { return kPriority; }
};

// 窗口事件（High 优先级）
struct WindowResizeEvent : IEvent {
    static constexpr uint32_t kTypeId = EventTypeId<WindowResizeEvent>::id();
    static constexpr EventPriority kPriority = EventPriority::High;

    int width;
    int height;

    uint32_t typeId() const override { return kTypeId; }
    EventPriority priority() const override { return kPriority; }
};

struct WindowCloseEvent : IEvent {
    static constexpr uint32_t kTypeId = EventTypeId<WindowCloseEvent>::id();
    static constexpr EventPriority kPriority = EventPriority::High;
    uint32_t typeId() const override { return kTypeId; }
    EventPriority priority() const override { return kPriority; }
};

// 资源事件（Low 优先级）
struct ResourceLoadCompleteEvent : IEvent {
    static constexpr uint32_t kTypeId = EventTypeId<ResourceLoadCompleteEvent>::id();
    static constexpr EventPriority kPriority = EventPriority::Low;

    uint64_t resourceId;
    bool success;
    std::string errorMessage;

    uint32_t typeId() const override { return kTypeId; }
    EventPriority priority() const override { return kPriority; }
};

// 音频事件（Normal 优先级）
struct AudioPlayEvent : IEvent {
    static constexpr uint32_t kTypeId = EventTypeId<AudioPlayEvent>::id();
    static constexpr EventPriority kPriority = EventPriority::Normal;

    uint64_t clipId;
    float volume = 1.0f;

    uint32_t typeId() const override { return kTypeId; }
    EventPriority priority() const override { return kPriority; }
};
```

---

## 4. 对象池（EventPool）

### 4.1 设计目标

- 热路径零堆分配
- 预分配 + 回收复用
- 线程安全（可选）

### 4.2 接口设计

```cpp
template<typename T, size_t PreAlloc = 64, size_t MaxCache = 256>
class EventPool {
public:
    ~EventPool() {
        for (auto* p : m_cache) delete p;
        for (auto* p : m_allocated) delete p;
    }

    // 从池中获取对象
    template<typename... Args>
    T* alloc(Args&&... args) {
        T* obj = nullptr;
        if (!m_cache.empty()) {
            obj = m_cache.back();
            m_cache.pop_back();
            *obj = T(std::forward<Args>(args)...);
        } else {
            obj = new T(std::forward<Args>(args)...);
            m_allocated.push_back(obj);
        }
        return obj;
    }

    // 归还到池中
    void free(T* obj) {
        if (m_cache.size() < MaxCache) {
            m_cache.push_back(obj);
        } else {
            auto it = std::find(m_allocated.begin(), m_allocated.end(), obj);
            if (it != m_allocated.end()) {
                m_allocated.erase(it);
            }
            delete obj;
        }
    }

    // 批量归还
    void freeAll(std::span<T*> objects) {
        for (T* obj : objects) {
            free(obj);
        }
    }

    size_t cacheSize() const { return m_cache.size(); }
    size_t allocatedSize() const { return m_allocated.size(); }

private:
    std::vector<T*> m_cache;      // 可复用对象
    std::vector<T*> m_allocated;   // 曾在用但未在缓存中的
};
```

### 4.3 全局事件池

```cpp
class GlobalEventPool {
public:
    static GlobalEventPool& instance() {
        static GlobalEventPool inst;
        return inst;
    }

    template<typename T, typename... Args>
    T* newEvent(Args&&... args) {
        return getPool<T>().alloc(std::forward<Args>(args)...);
    }

    template<typename T>
    void deleteEvent(T* obj) {
        getPool<T>().free(obj);
    }

    // RAII 事件句柄
    template<typename T>
    class EventGuard {
    public:
        EventGuard(T* obj) : m_obj(obj) {}
        ~EventGuard() { GlobalEventPool::instance().deleteEvent(m_obj); }
        T* get() const { return m_obj; }
        T* operator->() const { return m_obj; }

    private:
        T* m_obj;
    };

private:
    template<typename T>
    EventPool<T>& getPool() {
        static EventPool<T> pool;
        return pool;
    }
};
```

---

## 5. 监听器与订阅

### 5.1 Listener 结构

```cpp
template<typename T>
struct Listener {
    uint64_t id;                          // 唯一ID，用于取消订阅
    std::function<void(const T&)> handler; // 处理函数

    void invoke(const T& event) const {
        handler(event);
    }
};
```

### 5.2 订阅/取消订阅

```cpp
class EventBus {
public:
    // 订阅事件
    // 返回 listenerId，用于取消订阅
    template<typename T>
    uint64_t on(std::function<void(const T&)> handler) {
        auto id = m_nextListenerId++;
        auto& listeners = getListeners<T>();
        listeners.push_back({id, std::move(handler)});
        return id;
    }

    // 取消订阅
    template<typename T>
    void off(uint64_t listenerId) {
        auto& listeners = getListeners<T>();
        listeners.erase(
            std::remove_if(listeners.begin(), listeners.end(),
                [listenerId](const Listener<T>& l) { return l.id == listenerId; }),
            listeners.end()
        );
    }

    // 一次性事件（触发后自动取消）
    template<typename T>
    uint64_t once(std::function<void(const T&)> handler) {
        auto id = m_nextListenerId++;
        auto& listeners = getListeners<T>();
        listeners.push_back({id, [handler, id, this](const T& e) {
            handler(e);
            this->off<T>(id);
        }});
        return id;
    }

    // 优先级订阅（指定监听器优先级，影响调用顺序）
    template<typename T>
    uint64_t onWithPriority(std::function<void(const T&)> handler, int priority = 0) {
        auto id = m_nextListenerId++;
        auto& listeners = getListeners<T>();
        listeners.push_back({id, std::move(handler), priority});
        // 按 priority 排序
        std::sort(listeners.begin(), listeners.end(),
            [](const Listener<T>& a, const Listener<T>& b) {
                return a.priority < b.priority;
            });
        return id;
    }

private:
    template<typename T>
    std::vector<Listener<T>>& getListeners() {
        static std::vector<Listener<T>> listeners;  // 模板特化存储
        return listeners;
    }

    std::atomic<uint64_t> m_nextListenerId{1};
};
```

### 5.3 使用示例

```cpp
// 订阅
uint64_t jumpListener = EventBus::on<InputActionEvent>([](const InputActionEvent& e) {
    if (e.action == "Jump" && e.pressed) {
        player.jump();
    }
});

// 带优先级订阅（priority 越小越先调用）
EventBus::onWithPriority<InputActionEvent>([](const InputActionEvent& e) {
    // 这个会先执行
}, 0);

EventBus::onWithPriority<InputActionEvent>([](const InputActionEvent& e) {
    // 这个后执行
}, 1);

// 取消订阅
EventBus::off<InputActionEvent>(jumpListener);

// 一次性事件
EventBus::once<WindowCloseEvent>([](const WindowCloseEvent&) {
    std::cout << "窗口关闭，只触发一次\n";
});
```

---

## 6. 事件发布

### 6.1 即时发布（同步）

```cpp
class EventBus {
public:
    // 即时分发（同步调用所有监听器）
    template<typename T>
    void emit(const T& event) {
        auto& listeners = getListeners<T>();
        for (const auto& listener : listeners) {
            listener.invoke(event);
        }
    }

    // 带参数构造的事件（更方便）
    template<typename T, typename... Args>
    void emit(Args&&... args) {
        emit(T{std::forward<Args>(args)...});
    }

    // 指定优先级分发（覆盖事件默认优先级）
    template<typename T>
    void emit(const T& event, EventPriority priority) {
        // 从对应优先级队列取出监听器
        auto& listeners = getPriorityListeners<T>(priority);
        for (const auto& listener : listeners) {
            listener.invoke(event);
        }
    }
};
```

### 6.2 延迟发布（队列）

```cpp
class EventBus {
public:
    // 入队（延迟到 frame() 时分发）
    template<typename T>
    void enqueue(const T& event) {
        std::lock_guard<std::mutex> lock(m_queueMutex);

        // 使用对象池分配
        auto* packet = m_packetPool.alloc<EventPacket>();
        packet->typeId = T::kTypeId;
        packet->priority = event.priority();
        packet->event = GlobalEventPool::instance().newEvent<T>(event);

        // 按优先级插入
        auto& queue = m_queues[static_cast<size_t>(packet->priority)];
        queue.push(packet);
    }

    // 每帧调用，处理队列
    void frame() {
        // 按优先级顺序处理
        for (size_t p = 0; p < kPriorityCount; ++p) {
            auto& queue = m_queues[p];
            while (!queue.empty()) {
                auto* packet = queue.front();
                queue.pop();
                dispatch(packet);
                m_packetPool.free(packet);
            }
        }
    }

private:
    void dispatch(EventPacket* packet) {
        // 根据 typeId 找到对应监听器并调用
        // 使用类型擦除的监听器表
        auto it = m_typeListeners.find(packet->typeId);
        if (it != m_typeListeners.end()) {
            it->second->invoke(packet);
        }
    }

    struct EventPacket {
        uint32_t typeId;
        EventPriority priority;
        IEvent* event;  // 从全局池分配
    };

    std::array<std::queue<EventPacket*>, kPriorityCount> m_queues;
    EventPool<EventPacket> m_packetPool;
    std::mutex m_queueMutex;

    // 类型到监听器表的映射（用于延迟分发）
    std::unordered_map<uint32_t, IListenerTable*> m_typeListeners;
};
```

---

## 7. 优先级分发

### 7.1 优先级层级

```cpp
// 优先级数字越小越先执行
enum class EventPriority : uint8_t {
    Immediate = 0,  // 物理/输入，必须立即处理
    High = 1,       // 渲染/UI，高频响应
    Normal = 2,     // 游戏逻辑，常规处理
    Low = 3,        // 资源加载/后台任务，可延迟

    Count           // 数量统计
};
```

### 7.2 分发顺序

```
emit() 调用时：
1. 按 EventPriority 从 Immediate → Low 顺序查找监听器
2. 同优先级内按注册顺序调用
3. once() 监听器调用后自动移除

enqueue() 入队时：
1. 事件按优先级分类存储
2. frame() 时按 Immediate → Low 顺序批量分发
```

---

## 8. 与模块的接口

### 8.1 AYDevice (输入)

```cpp
// 输入设备发布事件
class DeviceManager {
    void pollDevices() {
        // 键盘事件（Immediate）
        for (auto& keyEvent : m_keyboard->getKeyEvents()) {
            EventBus::emit(keyEvent);  // 同步分发
        }

        // 鼠标移动（Immediate）
        if (m_mouse->hasMoved()) {
            EventBus::emit(m_mouse->getMoveEvent());
        }

        // 资源加载（Low，延迟分发）
        EventBus::enqueue(ResourceLoadCompleteEvent{clipId, true});
    }
};
```

### 8.2 AYRenderer (渲染)

```cpp
// 渲染器订阅窗口事件
class AYRenderer {
    void onWindowResize(const WindowResizeEvent& e) {
        adapter()->resize(e.width, e.height);
    }
};

// 初始化时订阅
EventBus::on<WindowResizeEvent>([this](const WindowResizeEvent& e) {
    onWindowResize(e);
});
```

### 8.3 AYUI (UI)

```cpp
// UI 监听输入事件
class Widget {
    void onInputAction(const InputActionEvent& e) {
        if (e.action == "Click" && e.pressed) {
            handleClick();
        }
    }
};

EventBus::on<InputActionEvent>([this](const InputActionEvent& e) {
    onInputAction(e);
});
```

---

## 9. 目录结构

```
AYEventSystem/
├── design.md
├── CMakeLists.txt
├── include/
│   └── AYEventSystem/
│       ├── AYEventSystem.h      # 主入口
│       ├── EventBus.h           # 事件总线
│       ├── EventPool.h          # 对象池
│       ├── EventPriority.h      # 优先级定义
│       ├── IEvent.h             # 事件基类
│       └── Events/              # 预定义事件
│           ├── InputEvents.h
│           ├── WindowEvents.h
│           ├── ResourceEvents.h
│           └── AudioEvents.h
│
└── src/
    ├── AYEventSystem.cpp
    └── EventBus.cpp
```

---

## 10. 实现优先级

### Phase 1: 核心
- [ ] IEvent 基类 + EventPriority
- [ ] EventBus 单例
- [ ] 模板化订阅/发布 (on/off/once)
- [ ] 优先级分层 (onWithPriority)

### Phase 2: 队列 + 对象池
- [ ] EventPool 对象池
- [ ] GlobalEventPool 全局池
- [ ] enqueue() 延迟入队
- [ ] frame() 批量分发

### Phase 3: 优化
- [ ] 类型安全的监听器表
- [ ] 批量事件回收
- [ ] 性能统计工具

---

## 11. 参考

- [Godot Signals](https://docs.godotengine.org/en/stable/tutorials/scripting/signals.html)
- [Unity EventSystem](https://docs.unity3d.com/Packages/com.unity.eventsystem@1.0/manual/index.html)
- [Entt (Entity Component System)](https://github.com/skypjack/entt) - 事件系统参考
- [NanoSignal](https://github.com/Neargye/nano_signal) - 现代 C++ 信号/槽