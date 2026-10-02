#!/usr/bin/env python3
"""Loopback-only deterministic Stratum v1 acceptance mock for Phase C."""
import argparse, json, socket, time

p=argparse.ArgumentParser()
p.add_argument('--host', default='127.0.0.1')
p.add_argument('--port', type=int, default=3333)
p.add_argument('--scenario', choices=['basic','reconnect-pinned','epoch-invalidation'], default='basic')
a=p.parse_args()
if a.host not in ('127.0.0.1','::1','localhost'):
    raise SystemExit('mock server is intentionally loopback-only')

def send(f,obj):
    f.write((json.dumps(obj,separators=(',',':'))+'\n').encode()); f.flush()

def notify(job_id, ntime='78563412', clean=True):
    return {'id':None,'method':'mining.notify','params':[job_id,'00'*32,'01000000','',[], '20000000','1d00ffff',ntime,clean]}

def run_session(conn, mode):
    conn.settimeout(30)
    submit_count=0
    with conn, conn.makefile('rwb', buffering=0) as f:
        subscribed=authorized=False
        for raw in f:
            msg=json.loads(raw)
            method=msg.get('method')
            if method=='mining.subscribe':
                send(f,{'id':msg.get('id'),'result':[[['mining.notify','deadbeef'],['mining.set_difficulty','deadbeef']],'01020304',4],'error':None})
                subscribed=True
            elif method=='mining.authorize':
                send(f,{'id':msg.get('id'),'result':True,'error':None}); authorized=True
                if mode=='redirect':
                    send(f,{'id':None,'method':'client.reconnect','params':['redirect.invalid',9999,0]})
                    print('PASS reconnect redirect sent; waiting for pinned-endpoint reconnect', flush=True)
                    return 'redirected'
                if mode=='epoch':
                    send(f,{'id':None,'method':'mining.set_difficulty','params':[1.0]})
                    send(f,notify('job-old','78563411',False))
                    send(f,{'id':None,'method':'mining.set_extranonce','params':['0a0b0c0d',4]})
                    send(f,{'id':None,'method':'mining.set_difficulty','params':[2.0]})
                    send(f,notify('job-new','78563413',True))
                else:
                    send(f,{'id':None,'method':'mining.set_difficulty','params':[1.0]})
                    send(f,notify('mock-job','78563412',True))
            elif method=='mining.submit':
                params=msg.get('params') or []
                if len(params)!=5: raise RuntimeError(f'bad submit params: {params!r}')
                user,job_id,xn2,ntime,nonce=params
                if user!='test.worker': raise RuntimeError(f'unexpected user {user!r}')
                if len(nonce)!=8: raise RuntimeError(f'bad nonce encoding {nonce!r}')
                if mode=='epoch':
                    if job_id!='job-new': raise RuntimeError(f'stale/old job submitted: {job_id!r}')
                    if xn2!='00000000': raise RuntimeError(f'wrong post-set_extranonce xnonce2: {xn2!r}')
                    if ntime!='78563413': raise RuntimeError(f'wrong ntime endian/value: {ntime!r}')
                else:
                    if job_id!='mock-job': raise RuntimeError(f'wrong job submitted: {job_id!r}')
                    if xn2!='00000000': raise RuntimeError(f'wrong xnonce2: {xn2!r}')
                    if ntime!='78563412': raise RuntimeError(f'wrong ntime endian/value: {ntime!r}')
                submit_count += 1
                if submit_count > 1:
                    raise RuntimeError(f'--shares-limit 1 oversubmitted before shutdown: submit_count={submit_count}')
                print('SUBMIT',json.dumps(params), flush=True)
                send(f,{'id':msg.get('id'),'result':True,'error':None})
                print('PASS accepted mock share', flush=True)
                # Do not return immediately: keep reading until the miner closes.  This makes
                # already-pipelined extra submits visible and turns the historical overshoot into
                # a deterministic test failure.
            else:
                print('IGNORED',msg,flush=True)
        if not (subscribed and authorized):
            raise RuntimeError('handshake incomplete')
    if submit_count == 1:
        print('PASS strict shares-limit exactly one submit', flush=True)
        return 'submitted'
    if submit_count == 0:
        return 'closed'
    raise RuntimeError(f'unexpected submit count {submit_count}')

family=socket.AF_INET6 if ':' in a.host else socket.AF_INET
with socket.socket(family,socket.SOCK_STREAM) as srv:
    srv.setsockopt(socket.SOL_SOCKET,socket.SO_REUSEADDR,1)
    srv.settimeout(30)
    srv.bind((a.host,a.port)); srv.listen(2)
    print(f'LISTEN {a.host}:{a.port} scenario={a.scenario}', flush=True)
    if a.scenario=='reconnect-pinned':
        conn,addr=srv.accept(); print('CLIENT1',addr,flush=True)
        result=run_session(conn,'redirect')
        if result!='redirected': raise RuntimeError('first session did not execute redirect scenario')
        conn,addr=srv.accept(); print('CLIENT2_PINNED',addr,flush=True)
        result=run_session(conn,'basic')
        if result!='submitted': raise RuntimeError('pinned-endpoint reconnect did not submit')
        print('PASS reconnect stayed on CLI endpoint',flush=True)
    elif a.scenario=='epoch-invalidation':
        conn,addr=srv.accept(); print('CLIENT',addr,flush=True)
        if run_session(conn,'epoch')!='submitted': raise RuntimeError('epoch scenario did not submit')
        print('PASS stale epoch/job was not submitted',flush=True)
    else:
        conn,addr=srv.accept(); print('CLIENT',addr,flush=True)
        if run_session(conn,'basic')!='submitted': raise RuntimeError('basic scenario did not submit')
