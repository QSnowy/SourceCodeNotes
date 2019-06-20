## CFRunLoop源码阅读，加上自己的理解注释

### 关于RunLoop的理解

`RunLoop`，是一个事件处理环。能够让线程在接收到事件的时候，进行事件处理，没有事件的时候进入休眠，类似于`do{//do something}while(1)`，能够保证线程不被系统回收，但是在空闲的时候又不会让CPU空转浪费资源。

### RunLoop接收的事件
`RunLoop`接收的事件，一般来自于两种不同的sources，分别是`input sources`和`timer sources`。

`input sources`会派送异步事件，比如其他线程的消息或者是其他应用的消息，会调用`runUntilDate:`使`RunLoop`在某个时刻退出。
`timer sources`会派送同步事件，由定时任务触发。

来个官方图：
![image](runloop_structure.jpg)

### RunLoop与RunLoopModes
RunLoop每次运行，都会有且仅有一个与之对应的`RunLoopMode`，`RunLoopMode`是`input sources`和`timers`以及`observers`的集合体。当`RunLoop`运行在指定的mode下时，与该mode有关的`sources`会派发事件、`observers`会收到相应的通知，而其他与当前mode无关的`sources`会被挂起。

举个例子：常见的`NSTimer`，创建后默认与`NSDefaultRunLoopMode`关联，当屏幕滑动时，主线程的`RunLoop`会切换到`NSEventTrackingRunLoopMode`，timer就会被挂起，不会触发定时任务了。

### RunLoop与线程
`RunLoop`与线程是一一对应的关系，主线程在启动的时候会自动让`RunLoop`运行，而二级线程的`RunLoop`则需要我们手动运行。另外还需要保证在二级线程中有`timer`或者`input sources`任务，不然`RunLoop`会立即停止，线程被系统回收。

手动启动runLoop的接口：
```Swift
[runLoop run];
[runLoop runUntilDate:limitDate];
[runLoop runMode:mode beforeDate:limitDate];
```

### RunLoop运行周期

1. 进入loop，通知观察者
2. 准备执行timer，通知观察者
3. 准备执行自定义source(source0)任务，通知观察者
4. 开始执行自定义source(source0)任务
5. 如果有待执行的source1任务，去9
6. 将要进入休眠，通知观察者
7. 进入休眠，直到有下列事件发生：
   - 基于端口的事件(source1事件)
   - 定时任务到达时间点
   - 到了设置的超时时间点
   - 被手动唤醒
8. 被唤醒了，通知观察者
9. 执行任务
   - timer，执行任务并且重启run loop，去到2
   - source，派发事件
   - 被唤醒且未到达超时时间，重启run loop，去2
10. 退出loop，通知观察者
