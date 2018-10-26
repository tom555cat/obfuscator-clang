# obfuscator-clang
混淆Objective-C的前端工具

对Objective-C中方法名增加前缀“zdd_”进行混淆，详细文档见:
https://www.jianshu.com/p/3a8fb6f7c55f

直接下载可执行文件ClangAutoStats进行函数名字替换：
```
./ClangAutoStats SourceFilesDirectory -- -ferror-limit=9999999 -ObjC
```

选项"-ferror-limit=9999999"避免错误过多停止执行；选项"-ObjC"是在头文件.h替换函数名时需要，否则头文件中的Objective-C方法不会识别出来。

