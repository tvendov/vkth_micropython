from machine import LCD
import framebuf
import lvgl as lv
import lv_utils

SuppRes = [
    (480, 272)
]

class pRGB(framebuf.FrameBuffer):

    def __init__(self, res=SuppRes[0]):
        if res not in SuppRes:
            raise ValueError('Unsupported resolution %s; the driver supports: %s.'%(str(res),', '.join(str(r) for r in SuppRes)))

        self.width = res[0]   #480
        self.height = res[1]  #272
        self.display = LCD()
        super().__init__(self.display, self.width, self.height, framebuf.RGB565)
        self.display.init()
        self.display.start()

class pRGB_lvgl(object):
    '''LVGL wrapper for paralel RGB LCD, not to be instantiated directly.

    * creates and registers LVGL display driver;
    * allocates buffers (single-buffered by default);
    * sets the driver callback to the disp_drv_flush_cb method.

    '''
    def disp_drv_flush_cb(self,disp_drv,area,color_p):
        # print(f"({area.x1},{area.y1}..{area.x2},{area.y2})")
        w = area.x2-area.x1+1
        h = area.y2-area.y1+1

        # blit in background
        data_view = color_p.__dereference__(w*h*self.pixel_size)
        self.blit(framebuf.FrameBuffer(data_view, w, h, framebuf.RGB565), area.x1, area.y1)
        self.disp_drv.flush_ready()

    def touch_drv_read_cb(self,touch_drv,data):
        self.points = self.display.touched()
        if(self.points > 0):
            dots = self.display.touches()
            data.point = lv.point_t({'x': dots[self.points-1][0], 'y': dots[self.points-1][1]})
            data.state = lv.INDEV_STATE.PRESSED
        else:
            data.state = lv.INDEV_STATE.RELEASED

    def __init__(self,doublebuffer=False,factor=10):
        if lv.COLOR_DEPTH != 16:
            raise RuntimeError(f'LVGL must be compiled with LV_COLOR_DEPTH=16 (currently LV_COLOR_DEPTH={lv.COLOR_DEPTH}).')

        color_format = lv.COLOR_FORMAT.RGB565
        self.pixel_size = lv.color_format_get_size(color_format)
        bufSize = (self.width*self.height*self.pixel_size)//factor

        if not lv.is_initialized(): lv.init()
        # create event loop if not yet present
        if not lv_utils.event_loop.is_running(): self.event_loop=lv_utils.event_loop()

        # attach all to self to avoid objects' refcount dropping to zero when the scope is exited
        self.buf1 = bytearray(bufSize)
        self.buf2 = bytearray(bufSize) if doublebuffer else None
        self.disp_drv = lv.display_create(self.width, self.height)
        self.disp_drv.set_color_format(color_format)
        self.disp_drv.set_flush_cb(self.disp_drv_flush_cb)
        self.disp_drv.set_buffers(self.buf1, self.buf2, bufSize, lv.DISPLAY_RENDER_MODE.PARTIAL)
        lv.theme_default_init(
            self.disp_drv,
            lv.palette_main(lv.PALETTE.BLUE),
            lv.palette_main(lv.PALETTE.RED),
            False,
            lv.font_montserrat_14,
        )

        self.points = 0
        self.indev_drv = lv.indev_create()
        self.indev_drv.set_type(lv.INDEV_TYPE.POINTER)
        self.indev_drv.set_display(self.disp_drv)
        self.indev_drv.set_read_cb(self.touch_drv_read_cb)

class RGB(pRGB,pRGB_lvgl):
    def __init__(self,res=SuppRes[0],doublebuffer=False,factor=10,**kw):
        '''See :obj:`St77xx_hw` for the meaning of the parameters.'''
        pRGB.__init__(self,res)
        pRGB_lvgl.__init__(self,doublebuffer,factor)
