"""Run a compiled logger_test executable in fresh processes for each LOG_LEVEL."""
import json,os,subprocess,sys

exe=sys.argv[1]
for level in [None,'DEBUG','INFO','WARN','ERROR','OFF','invalid','debug']:
    env=os.environ.copy()
    env.pop('LOG_LEVEL',None)
    if level is not None:env['LOG_LEVEL']=level
    result=subprocess.run([exe],env=env,capture_output=True,text=True,timeout=20)
    assert result.returncode==0,(level,result.returncode,result.stderr)
    assert 'never-print' not in result.stderr
    rows=[json.loads(line) for line in result.stderr.splitlines()]
    for row in rows:
        assert all(k in row for k in ['timestamp','level','thread_id','component','event','fields']),row
        assert row['timestamp'].endswith('Z')
    current=level.upper() if level else 'INFO'
    if current=='OFF':assert not rows
    elif current in ['WARN','ERROR']:
        assert all(row['level'] in (['WARN','ERROR'] if current=='WARN' else ['ERROR']) for row in rows)
    else:
        fields=next(row['fields'] for row in rows if row['event']=='fields')
        assert fields['count']==12 and fields['fd']==-1 and fields['success'] is True
        assert fields['session_id']==18446744073709551615
        assert fields['reason']=='quoted"\nline\r\\end'
        assert all(fields[k]=='[REDACTED]' for k in ['password','authorization','packet','login_ticket'])
        parallel=[row for row in rows if row['event']=='parallel']
        assert len(parallel)==320
        assert len({(r['fields']['worker_id'],r['fields']['count']) for r in parallel})==320
        assert len(next(r['fields']['reason'] for r in rows if r['event']=='bounded'))==195
        if current=='DEBUG':
            begin={r['fields']['operation_id'] for r in rows if r['event']=='scope_begin'}
            end={r['fields']['operation_id'] for r in rows if r['event']=='scope_end'}
            assert begin==end and len(begin)==2
            assert any(r['event']=='scope_end' and r['fields']['outcome']=='exception' and r['level']=='ERROR' for r in rows)
        else:
            assert not any(r['level']=='DEBUG' for r in rows)
            assert any(r['event']=='scope_exception' for r in rows)
    print(f'PASS LOG_LEVEL={level}: {len(rows)} complete JSON records')
print('PASS: filtering, concurrent line integrity, bounded escaping, redaction, errno, scope timing and failed sink')
