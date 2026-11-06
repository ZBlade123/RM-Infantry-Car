import asyncio, sys
LISTEN=('127.0.0.1',443); TARGET=('20.205.243.166',443)
async def pipe(r,w):
    try:
        while True:
            d=await r.read(65536)
            if not d: break
            w.write(d); await w.drain()
    except Exception: pass
    finally:
        try: w.close()
        except Exception: pass
async def handle(cr,cw):
    try: sr,sw=await asyncio.open_connection(*TARGET)
    except Exception as e:
        cw.close(); return
    await asyncio.gather(pipe(cr,sw),pipe(sr,cw))
async def main():
    s=await asyncio.start_server(handle,*LISTEN)
    print('tunnel up',LISTEN,'->',TARGET,flush=True)
    async with s: await s.serve_forever()
asyncio.run(main())