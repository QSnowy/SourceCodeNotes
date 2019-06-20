## CFRunLoop源码阅读，加上自己的理解注释

### 关于RunLoop的理解

RunLoop，是一个事件处理环。能够让线程在接收到事件的时候，进行事件处理，没有事件的时候进入休眠，类似于`do{//do something}while(1)`。RunLoop接收的事件，一般来自于两种不同的sources，分别是`input sources`和`timer sources`。

`input sources`会派送异步事件，比如其他线程的消息或者是其他应用的消息，会调用`runUntilDate:`使RunLoop退出。
`timer sources`会派送同步事件，由定时任务触发。

来个官方图：
![image](runloop_structure.jpg)

### RunLoop与线程的关系
线程与RunLoop是一一对应的关系，主线程在启动的时候会自动让RunLoop运行，而其他线程则需要我们手动调起。

### RunLoop与RunLoopModes
RunLoop每次运行，都会有与之对应的mode，`RunLoopMode`是`input sources`和`timers`以及`observers`的集合体，

