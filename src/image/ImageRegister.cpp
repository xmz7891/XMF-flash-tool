#include "../app/ToolModules.h"

namespace gf {

// 工具注册按主题拆分到多个翻译单元，全部由本函数统一挂载。
void registerImageFirmwareTools();
void registerImageConvertTools();

// Image/firmware tools are registered here by the image-core teammate.
// Implement them in separate translation units inside src/image/ and call
// registerImageTools() from this file.
void registerImageTools()
{
    registerImageFirmwareTools();
    registerImageConvertTools();
}

} // namespace gf
