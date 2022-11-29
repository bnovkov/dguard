from group import Group
from element import Element
from random import sample

if __name__=='__main__':
    groups = [Group(i, []) for i in range(5)]
    for i in range(10):
        element = Element(i+6)
        groupId = sample(range(0, 5), 2)
        for j in groupId:
            groups[j].add(element)
    for group in groups:
        print(group)
    



    