from element import Element
from random import sample

LABEL_SIZE = 10
def int2bin(number):
    return '{0:010b}'.format(number)

class System:
    def __init__(self, loadNodes, storeNodes):
        self.groups = [Group(i, []) for i in range(loadNodes)]
        for i in range(storeNodes):
            node = Element(i)
            #print("kreiran element", node)
            groupId = sample(range(0, loadNodes), 2)
            #print("odabrani indexi grupa u koji ce se taj element dodati", groupId)
            for j in groupId:
                #print("\tdodavanje u grupu", j)
                #print("prije dodavanja\n", self.groups[j])
                

                self.groups[j].add(node)
                #print("nakon dodavanja\n", self.groups[j])

    def optimizeLabel(self, startingLabel, mask, labels):
        ret = startingLabel
        for i in range(len(mask)):
            hammingDis = self.hammingDistance(ret, labels[0])
            reseted = False
            for label in labels:
                if(hammingDis != self.hammingDistance(ret, label)):
                    symbol = '1' - ('1' - startingLabel[i]) #ako je "0" pretvori u "1" i obratno
                    ret = startingLabel[:i-1] + symbol + startingLabel[i+1:]
                    reseted = True
                    break
            if(not reseted):
                return ret

    def generateLabel(self, labels):
        ret = ''
        mask = 0
        for label in labels:
            mask = mask^label
        
        mask = int2bin(mask)
        index = []
        for i in range(len(mask)):
            if(mask[i]):
                index.append(i)
        for i in range(LABEL_SIZE):
            zeroCount = 0
            oneCount = 0
            for label in labels:
                if(int2bin(label[i])):
                    oneCount +=1
                else:
                    zeroCount +=1
            if(zeroCount >= oneCount):
                ret += '0'
            else:
                ret += '1'

        if(len(labels) == 0):
            #TODO generiraj nasumicnu labelu
            return -1

        return self.optimizeLabel(ret, index, labels)




    def hammingDistance(self, num1, num2):
        return(bin(num1^num2).count("1"))


    def labelGroup(self, index):
        setLabels = []
        for element in self.groups[index]:
            if(element.label != ''): setLabels.append(element.label)
        groupLabel = ''
        if(len(setLabels) != 0):
            groupLabel = self.groups[index].generateLabel(setLabels)

    



    def __str__(self) -> str:
        usedElements = []
        extraConnections = []
        ret = 'digraph G{\n'
        for group in self.groups:
            ret += "\tsubgraph cluster_L{0} {{\n".format(group.mainElement.id)
            ret +="\t\tstyle=filled;\n\t\tcolor=lightgrey;\n\t\tnode [style=filled,color=white];\n"
            for element in group.elements:
                if(element.id not in usedElements):
                    ret += "\t\tS{} -> L{};\n".format(element.id, group.mainElement.id)
                    usedElements.append(element.id)
                else:
                    extraConnections.append([group.mainElement.id, element.id])
            
            ret+='\t\tlabel = \"podgrupa {};\"\n\t}}\n'.format(group.mainElement.id)

        for connection in extraConnections:
            ret += "\tS{} -> L{};\n".format(connection[1], connection[0])
        ret += "}"
        return ret

class Group:
    def __init__(self, id, elements = []):
        self.mainElement = Element(id)
        self.elements = elements
        self.treshold = len(elements)
    def add(self, element):
        self.elements.append(element)
        self.treshold += 1
    def updateTreshold(self):
        self.treshold = len(self.elements)

    def generateLabels(self, labels):
        #TODO treba iskonstruirati labelu load naredbe na temelju svih postavljenih store labela store naredbi iz proslih skupina
        pass

    def __str__(self) -> str:
        ret = str(self.mainElement.id)
        ret += "\n"
        for element in self.elements:
            ret += element.__str__() + " "
        return ret + "\n"

if __name__=='__main__':
    system = System(5, 7)
    print(int2bin(3^8))

    





