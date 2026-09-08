import sdr_single as s,time,gc
s.save_params=lambda p:None
a=s.start()
iq=a.be.iq
assert a.be.running,a.be.err
def act(fn):
    result=[0,None]
    def cb(t):
        t.pause()
        try:fn()
        except Exception as e:result[1]=repr(e)
        result[0]=1
    t=s.lv.timer_create(cb,1,None)
    deadline=time.ticks_add(time.ticks_ms(),3000)
    while not result[0] and time.ticks_diff(deadline,time.ticks_ms())>0:time.sleep_ms(10)
    t.delete()
    assert result[0],'GUI callback timeout'
    assert result[1] is None,result
def click(w):act(lambda:w.send_event(s.lv.EVENT.CLICKED,None))
time.sleep_ms(1500)
gc.collect()
print('SMALL_HOME_FREE',gc.mem_free())
click(a.ui.get('brand-row'))
click(a._route_widgets['backend-button'])
print('SMALL_VERIFY_RESULT',a.be.err,gc.mem_free(),a.ui.get('scr-settings'))
assert s.lv.screen_active()==a.ui.get('scr-settings'),a.be.err

def select(text):
    click(a._set_widgets['tone_monitor'][0])
    panel=s._KEEP['pick_menu'][0].get_child(0)
    for k in range(1,panel.get_child_count()):
        b=panel.get_child(k)
        if b.get_child(0).get_text()==text:
            click(b)
            return
    raise RuntimeError(text)
iq.demod('am');iq.tune(3000);iq.bandwidth(6000)
iq.inject(1,3000,400,1,1750,50,0,0,0,0,0)
select('1750 Hz')
time.sleep_ms(1200)
act(a._paint_tone_monitor)
assert iq.tone_monitor()[1],iq.tone_monitor()
assert a._set_widgets['tone_monitor'][1].get_text()=='TONE'
print('UI_1750_TONE',iq.tone_monitor())
select('1000 Hz')
time.sleep_ms(1200)
act(a._paint_tone_monitor)
assert not iq.tone_monitor()[1]
assert a._set_widgets['tone_monitor'][1].get_text()=='WAIT'
select('OFF')
assert iq.tone_monitor()==(0,False,0,0,False)
click(a.ui.get('scr-settings').get_child(0).get_child(2))
assert s.lv.screen_active()==a.ui.get('scr-receiver')
print('RX_TONE_GUI_PASS',gc.mem_free())
