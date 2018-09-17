#include "clang/Driver/Options.h"
#include "clang/AST/AST.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/ASTConsumer.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Frontend/ASTConsumers.h"
#include "clang/Frontend/FrontendActions.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Tooling/CommonOptionsParser.h"
#include "clang/Tooling/Tooling.h"
#include "clang/Rewrite/Core/Rewriter.h"
#include "clang/Rewrite/Frontend/FixItRewriter.h"
#include <memory>
#include <utility>
using namespace std;
using namespace clang;
using namespace clang::driver;
using namespace clang::tooling;
using namespace llvm;
//Rewriter rewriter;
int numFunctions = 0;
class ExampleVisitor : public RecursiveASTVisitor<ExampleVisitor> {
private:
    //ASTContext *astContext; // used for getting additional AST info
    //typedef clang::RecursiveASTVisitor<RewritingVisitor> Base;
    Rewriter &rewriter;
public:
    explicit ExampleVisitor(Rewriter &R)
    : rewriter{R} // initialize private members
    {}
    
    // 判断函数是否能够混淆
    bool canObfuscate(ObjCMethodDecl *MD) {
        // 如果该方法是协议方法，不进行混淆
        ObjCInterfaceDecl *ID = MD->getClassInterface();
        if (!ID) {
            return false;
        }
        for (ObjCProtocolDecl *protocol : ID->all_referenced_protocols()) {
            if (protocol->lookupMethod(MD->getSelector(), MD->isInstanceMethod())) {
                return false;
            }
        }
        
        // 不混淆读写方法/系统方法/init前缀方法/set前缀方法/zdd_前缀方法
        string methodName = MD->getNameAsString();
        if (MD->isPropertyAccessor() || isInSystem(MD) || methodName.find("set") == 0 || methodName.find("init") == 0 || methodName.find("zdd_") == 0) {
            return false;
        }
        
        return true;
    }
    
    bool VisitObjCMethodDecl(ObjCMethodDecl *D) {
        this->renameFunctionName(D);
        return true;
    }
    
    // 修改函数调用
    bool VisitObjCMessageExpr(ObjCMessageExpr *messageExpr) {
        // 跳过系统类
        ObjCMethodDecl *MD = messageExpr->getMethodDecl();
        if (MD) {
            if(canObfuscate(MD) == false) {
                return true;
            }
            Selector selector = messageExpr->getSelector();
            // 方法是通过.调用还是通过发消息调用
            string funcNameWithPrefix = "zdd_" + selector.getNameForSlot(0).str();
            errs() << "first selector slot size:" << selector.getNameForSlot(0).size() << "\n";
            rewriter.ReplaceText(messageExpr->getSelectorStartLoc(),
                                 selector.getNameForSlot(0).size(),
                                 funcNameWithPrefix);
        }
        return true;
    }
    
    // 修改函数声明处的函数名字
    void renameFunctionName(ObjCMethodDecl *MD) {
        // 判断是否应该混淆方法名
        if (canObfuscate(MD) == false) {
            return;
        }
        string funcName = MD->getNameAsString();
        
        Selector selector = MD->getSelector();
        string funcNameWithPrefix = "zdd_" + selector.getNameForSlot(0).str();
        rewriter.ReplaceText(MD->getSelectorStartLoc(), selector.getNameForSlot(0).size(), funcNameWithPrefix);
    }
    
    bool isInSystem(Decl *decl) {
        SourceManager &SM = rewriter.getSourceMgr();
        if (SM.isInSystemHeader(decl->getLocation()) ||
            SM.isInExternCSystemHeader(decl->getLocation())) {
            return true;
        }
        return false;
    }
};
class ExampleASTConsumer : public ASTConsumer {
private:
    ExampleVisitor visitor; // doesn't have to be private
    
public:
    // override the constructor in order to pass CI
    explicit ExampleASTConsumer(Rewriter &R)
    : visitor(R) // initialize the visitor
    { }
    
    // override this to call our ExampleVisitor on the entire source file
    virtual void HandleTranslationUnit(ASTContext &Context) {
        /* we can use ASTContext to get the TranslationUnitDecl, which is
         a single Decl that collectively represents the entire source file */
        visitor.TraverseDecl(Context.getTranslationUnitDecl());
    }
};
class ExampleFrontendAction : public ASTFrontendAction {
    
private:
    Rewriter rewriter;
public:
    virtual unique_ptr<ASTConsumer> CreateASTConsumer(CompilerInstance &CI, StringRef file) {
        rewriter.setSourceMgr(CI.getSourceManager(), CI.getLangOpts());
        CI.getPreprocessor();
        return make_unique<ExampleASTConsumer>(rewriter);
    }
    
    void EndSourceFileAction() override {
        SourceManager &SM = rewriter.getSourceMgr();
        llvm::errs() << "** EndSourceFileAction for: "
        << SM.getFileEntryForID(SM.getMainFileID())->getName() << "\n";
        
        // Now emit the rewritten buffer.
        string Filename = SM.getFileEntryForID(SM.getMainFileID())->getName();
        std::error_code error_code;
        llvm::raw_fd_ostream outFile(Filename, error_code, llvm::sys::fs::F_None);
        rewriter.getEditBuffer(SM.getMainFileID()).write(outFile);
        //rewriter.getEditBuffer(SM.getMainFileID()).write(llvm::outs());
    }
};
/// Return if filePath a code file(.h/.m/.c/.cpp)
bool hasSuffix(string rawStr, string suffix) {
    return rawStr.find(suffix) == (rawStr.length() - suffix.length());
}
bool isCodeFile(string filePath, bool ignoreHeader) {
    if (hasSuffix(filePath, ".h")) {
        return !ignoreHeader;
    } else {
        return hasSuffix(filePath, ".m");
    }
}
/// Get all .h/.m/.cpp/.c files in rootDir
void getFilesInDir(string rootDir, vector<string> &files, bool ignoreHeader) {
    if (sys::fs::exists(rootDir)) {
        std::error_code code;
        sys::fs::recursive_directory_iterator end;
        for (auto it = sys::fs::recursive_directory_iterator(rootDir, code); it != end; it.increment(code)) {
            if (code) {
                llvm::errs() << "Error: " << code.message() << "\n";
                break;
            }
            
            string path = (*it).path();
            if (isCodeFile(path, ignoreHeader)) {
                files.push_back(getAbsolutePath(path));
            }
        }
        
    } else {
        llvm::errs() << "Directory " << rootDir << " not exists!\n";
    }
}
void getHeaderFilesInDir(string rootDir, vector<string> &files) {
    if (sys::fs::exists(rootDir)) {
        std::error_code code;
        sys::fs::recursive_directory_iterator end;
        for (auto it = sys::fs::recursive_directory_iterator(rootDir, code); it != end; it.increment(code)) {
            if (code) {
                llvm::errs() << "Error: " << code.message() << "\n";
                break;
            }
            
            string path = (*it).path();
            if (hasSuffix(path, ".h")) {
                files.push_back(getAbsolutePath(path));
            }
        }
        
    } else {
        llvm::errs() << "Directory " << rootDir << " not exists!\n";
    }
}
static cl::OptionCategory OptsCategory("ClangAutoStats");
int main(int argc, const char **argv) {
    
    // parse the command-line args passed to your code
    CommonOptionsParser op(argc, argv, OptsCategory);
    // create a new Clang Tool instance (a LibTooling environment)
    
    vector<string> commands;
    
    for (int i = 0; i < argc; ++i) {
        const char *arg = argv[i];
        if (sys::fs::is_directory(arg)) {
            // 先读取.m文件，然后读取.h文件，防止先修改.h文件，导致.m中的方法未定义
            getFilesInDir(arg, commands, true);
            getHeaderFilesInDir(arg, commands);
        } else if (sys::fs::is_regular_file(arg)) {
            string fullPath = string(argv[i]);
            commands.push_back(getAbsolutePath(fullPath));
        } else {
            commands.push_back(string(argv[i]));
        }
    }
    //getFilesInDir("/Users/tongleiming/Documents/develop/xxxxx", commands, false);
    //getFilesInDir("/Users/tongleiming/Documents/develop/xxxxx", commands, false);
//    commands.push_back("/Users/tongleiming/Documents/test/RewriteDir/Hello1.m");
//    commands.push_back("/Users/tongleiming/Documents/test/RewriteDir/Hello2.m");
//    commands.push_back("/Users/tongleiming/Documents/test/RewriteDir/Hello1.h");
    
    
    ClangTool Tool(op.getCompilations(), commands);
    
    // run the Clang Tool, creating a new FrontendAction (explained below)
    int result = Tool.run(newFrontendActionFactory<ExampleFrontendAction>().get());
    
    errs() << "\nFound " << numFunctions << " functions.\n\n";
    return result;
}
