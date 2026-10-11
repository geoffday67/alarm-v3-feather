Import("env")

import time
import serial


def reset_after_upload(source, target, env):
    port = env.subst("$UPLOAD_PORT")
    try:
        s = serial.Serial(port, 115200)
        time.sleep(0.3)
        s.close()
        print("post_upload_reset: reset pulse sent on %s" % port)
    except Exception as e:
        print("post_upload_reset: failed (%s)" % e)


env.AddPostAction("upload", reset_after_upload)
