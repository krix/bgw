l=open(r"E:\Projects\BGW\recreated\docs\decomp.txt").read().splitlines()
# find render loop start
for i,x in enumerate(l):
    if "454648" in x or "FUN_00453e50" in x and i>50 and i<150:
        print(i+1, x)
        break
