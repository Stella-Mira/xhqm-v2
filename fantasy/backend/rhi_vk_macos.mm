// macOS VK 表面辅助：从 Qt 传入的 NSView 指针取/建 CAMetalLayer（MoltenVK VK_EXT_metal_surface）
#import <Cocoa/Cocoa.h>
#import <QuartzCore/CAMetalLayer.h>

void* rhi_vk_macos_metal_layer(void* nsview_ptr)
{
	NSView* view = (__bridge NSView*)nsview_ptr;
	if (!view) return nullptr;
	CAMetalLayer* layer = (CAMetalLayer*)view.layer;
	if (![layer isKindOfClass:[CAMetalLayer class]])
	{
		view.wantsLayer = YES;
		layer = [CAMetalLayer layer];
		view.layer = layer;
	}
	return (__bridge void*)layer;
}
